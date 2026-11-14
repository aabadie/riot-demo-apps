/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       CLUE BLE Dashboard: the Adafruit CLUE and a Web Bluetooth page
 *              on the phone
 *
 * The board is a GATT server. The page of web/, opened in Chrome on Android
 * (or any browser with Web Bluetooth), connects to it and:
 *
 * - plots its accelerometer, temperature, humidity, pressure and light, 10
 *   times per second
 * - teaches it colors: hold an object against the color sensor and tap a
 *   slot on the phone. The board learns the color with an emlearn Gaussian
 *   naive Bayes model (the one of Color Sniffer) and recognizes it, the
 *   page lights up the slot and the board plays its note
 * - writes a message on the screen of the board, plays notes on its buzzer
 * - shows how far the phone is, from the signal strength of the
 *   connection, also shown on the screen of the board
 *
 * The buttons of the board show up on the page.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "apds99xx.h"
#include "apds99xx_params.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "mutex.h"
#include "nimble_autoadv.h"
#include "nimble_riot.h"
#include "periph/gpio.h"
#include "phydat.h"
#include "saul_reg.h"
#include "services/gap/ble_svc_gap.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "colors.h"
#include "protocol.h"

#ifndef LIGHT_PIN
#define LIGHT_PIN       GPIO_PIN(0, 10)     /* white LEDs of the color sensor */
#endif

#define FRAME_MS        (100U)
#define SLOW_MS         (1000U)     /* temperature, humidity, pressure, RSSI */
#define NEAR_PRX        (40U)
#define TEACH_SAMPLES   (10U)

#define COLOR_BG        GFX_RGB(10, 12, 22)
#define COLOR_PANEL     GFX_RGB(26, 30, 52)
#define COLOR_TEXT      GFX_RGB(230, 235, 255)
#define COLOR_DIM       GFX_RGB(120, 125, 160)
#define COLOR_ACCENT    GFX_RGB(80, 200, 255)
#define COLOR_GOOD      GFX_RGB(60, 220, 110)
#define COLOR_MATCH     GFX_RGB(255, 200, 60)

/* c1ae00XX-5d8a-4a43-9a3c-6f5c2b1e7d90 */
#define DASH_UUID(x)    BLE_UUID128_INIT(0x90, 0x7d, 0x1e, 0x2b, 0x5c, 0x6f, 0x3c, 0x9a, \
                                         0x43, 0x4a, 0x8a, 0x5d, (x), 0x00, 0xae, 0xc1)

static const ble_uuid128_t _uuid_service = DASH_UUID(DASH_UUID_SERVICE);
static const ble_uuid128_t _uuid_sensors = DASH_UUID(DASH_UUID_SENSORS);
static const ble_uuid128_t _uuid_status = DASH_UUID(DASH_UUID_STATUS);
static const ble_uuid128_t _uuid_text = DASH_UUID(DASH_UUID_TEXT);
static const ble_uuid128_t _uuid_teach = DASH_UUID(DASH_UUID_TEACH);
static const ble_uuid128_t _uuid_tone = DASH_UUID(DASH_UUID_TONE);

static const uint16_t _notes[DASH_SLOTS] = {
    NOTE_C5, NOTE_D5, NOTE_E5, NOTE_G5, NOTE_A5, NOTE_C6,
};

static struct {
    saul_reg_t *temp;
    saul_reg_t *hum;
    saul_reg_t *press;
} _saul;

static apds99xx_t _apds;
static bool _apds_ok;
static dash_sensors_t _sensors;
static dash_status_t _status;
static uint8_t _sensors_pkt[DASH_SENSORS_LEN];
static uint8_t _status_pkt[DASH_STATUS_LEN];

static uint16_t _sensors_handle, _status_handle;
static uint16_t _conn = BLE_HS_CONN_HANDLE_NONE;
static bool _notify_sensors, _notify_status;

/* written by the NimBLE host thread, read by main */
static mutex_t _lock = MUTEX_INIT;
static char _text[DASH_TEXT_MAX + 1];
static bool _text_changed;
static uint8_t _teach_cmd, _teach_slot;
static uint16_t _tone_freq, _tone_ms;
static bool _conn_changed;

/* teaching in progress, main thread only */
static unsigned _teach_now, _teach_left;

static int _access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def _services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &_uuid_service.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &_uuid_sensors.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &_sensors_handle,
            },
            {
                .uuid = &_uuid_status.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &_status_handle,
            },
            {
                .uuid = &_uuid_text.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &_uuid_teach.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &_uuid_tone.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 },
        },
    },
    { 0 },
};

static int _access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    uint8_t buf[DASH_TEXT_MAX * 4];
    uint16_t len = 0;
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        mutex_lock(&_lock);
        int res = ble_uuid_cmp(uuid, &_uuid_sensors.u) == 0 ?
                  os_mbuf_append(ctxt->om, _sensors_pkt, sizeof(_sensors_pkt)) :
                  os_mbuf_append(ctxt->om, _status_pkt, sizeof(_status_pkt));
        mutex_unlock(&_lock);
        return res ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    mutex_lock(&_lock);
    int res = 0;
    if (ble_uuid_cmp(uuid, &_uuid_text.u) == 0) {
        dash_clean_text(_text, buf, len);
        _text_changed = true;
    }
    else if (ble_uuid_cmp(uuid, &_uuid_teach.u) == 0 && len == 2 &&
             buf[0] >= DASH_TEACH_SAMPLES && buf[0] <= DASH_TEACH_CLEAR_ALL &&
             buf[1] < DASH_SLOTS) {
        _teach_cmd = buf[0];
        _teach_slot = buf[1];
    }
    else if (ble_uuid_cmp(uuid, &_uuid_tone.u) == 0 && len == 4) {
        _tone_freq = (uint16_t)(buf[0] | (buf[1] << 8));
        _tone_ms = (uint16_t)(buf[2] | (buf[3] << 8));
    }
    else {
        res = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    mutex_unlock(&_lock);
    return res;
}

static int _gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            nimble_autoadv_start(NULL);
            break;
        }
        _conn = event->connect.conn_handle;
        _conn_changed = true;
        puts("connected");
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        _conn = BLE_HS_CONN_HANDLE_NONE;
        _notify_sensors = _notify_status = false;
        _conn_changed = true;
        puts("disconnected");
        nimble_autoadv_start(NULL);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == _sensors_handle) {
            _notify_sensors = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == _status_handle) {
            _notify_status = event->subscribe.cur_notify;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void _notify(uint16_t handle, const uint8_t *data, size_t len)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);

    if (om) {
        ble_gatts_notify_custom(_conn, handle, om);
    }
}

static bool _read(saul_reg_t *dev, float *value, uint8_t *unit)
{
    phydat_t data;

    if (dev == NULL || saul_reg_read(dev, &data) <= 0) {
        return false;
    }
    *value = data.val[0] * powf(10.0f, data.scale);
    if (unit) {
        *unit = data.unit;
    }
    return true;
}

static void _read_slow(void)
{
    uint8_t unit;
    float v;

    if (!_read(_saul.temp, &_sensors.temp_c, NULL)) {
        _sensors.temp_c = NAN;
    }
    if (!_read(_saul.hum, &_sensors.hum, NULL)) {
        _sensors.hum = NAN;
    }
    _sensors.press_hpa = NAN;
    if (_read(_saul.press, &v, &unit)) {
        _sensors.press_hpa = unit == UNIT_PA ? v / 100.0f : v;
    }
    if (_conn != BLE_HS_CONN_HANDLE_NONE) {
        int8_t rssi = 0;
        _status.rssi = ble_gap_conn_rssi(_conn, &rssi) == 0 ? rssi : 0;
    }
    else {
        _status.rssi = 0;
    }
}

/* screen color of a reading, white balanced on its brightest channel */
static uint16_t _to_screen(const uint16_t rgb[3])
{
    uint32_t max = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) :
                   (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);

    if (max == 0) {
        return GFX_RGB(0, 0, 0);
    }
    return GFX_RGB(rgb[0] * 255 / max, rgb[1] * 255 / max, rgb[2] * 255 / max);
}

static void _draw_connection(void)
{
    char text[32];
    int w = gfx_width();

    gfx_fill_rect(0, 30, w, 20, COLOR_BG);
    if (_conn == BLE_HS_CONN_HANDLE_NONE) {
        gfx_text_centered(34, "WAITING FOR THE PHONE", COLOR_DIM, COLOR_BG, 1);
        return;
    }
    /* -40 dBm is next to the board, -90 dBm far away */
    int bars = _status.rssi ? (_status.rssi + 95) / 11 : 0;
    bars = bars < 0 ? 0 : (bars > 5 ? 5 : bars);
    for (int i = 0; i < 5; i++) {
        int h = 4 + 3 * i;
        gfx_fill_rect(10 + i * 8, 48 - h, 6, h, i < bars ? COLOR_GOOD : COLOR_PANEL);
    }
    snprintf(text, sizeof(text), "PHONE %d DBM", _status.rssi);
    gfx_text(60, 36, text, COLOR_TEXT, COLOR_BG, 1);
}

static void _draw_text(void)
{
    char line[21];
    size_t len = strlen(_text);

    gfx_fill_rect(0, 56, gfx_width(), 3 * 20, COLOR_PANEL);
    for (unsigned l = 0; l < 3 && l * 20 < len; l++) {
        size_t n = len - l * 20 < 20 ? len - l * 20 : 20;
        memcpy(line, _text + l * 20, n);
        line[n] = '\0';
        gfx_text_centered(60 + l * 20, line, COLOR_TEXT, COLOR_PANEL, 2);
    }
}

static void _draw_color(void)
{
    char text[24];
    int y = 124;

    gfx_fill_rect(8, y, 80, 80, _sensors.has_rgb ? _to_screen(_sensors.rgb) : COLOR_BG);
    gfx_fill_rect(96, y, gfx_width() - 96, 80, COLOR_BG);
    if (_status.match >= 0) {
        char big[2] = { (char)('1' + _status.match), '\0' };
        gfx_text(110, y + 8, big, COLOR_MATCH, COLOR_BG, 6);
    }
    else {
        gfx_text(110, y + 20, _status.match == DASH_MATCH_FAR ? "NO OBJECT" : "?",
                 COLOR_DIM, COLOR_BG, 2);
    }
    unsigned total = 0;
    for (unsigned i = 0; i < DASH_SLOTS; i++) {
        total += _status.counts[i];
    }
    snprintf(text, sizeof(text), "%u SAMPLES TAUGHT", total);
    gfx_text(110, y + 64, text, COLOR_DIM, COLOR_BG, 1);
}

static void _color_reading(void)
{
    static int last_match = -3;
    apds99xx_rgb_t rgb;
    uint16_t clear, prx = 0;
    float f[COLORS_FEATURES];

    if (!_apds_ok || apds99xx_data_ready_als(&_apds) != APDS99XX_OK ||
        apds99xx_read_rgb_raw(&_apds, &rgb) != APDS99XX_OK ||
        apds99xx_read_als_raw(&_apds, &clear) != APDS99XX_OK) {
        return;
    }
    apds99xx_read_prx_raw(&_apds, &prx);
    /* the APDS9960 has no lux conversion: the clear channel, in counts */
    _sensors.lux = clear;
    _sensors.has_rgb = true;
    _sensors.rgb[0] = rgb.red;
    _sensors.rgb[1] = rgb.green;
    _sensors.rgb[2] = rgb.blue;
    _status.proximity = prx;

    if (prx < NEAR_PRX) {
        _status.match = DASH_MATCH_FAR;
        _status.distance = 0;
    }
    else {
        colors_features(rgb.red, rgb.green, rgb.blue, clear, f);
        if (_teach_left) {
            colors_teach(_teach_now, f);
            _status.counts[_teach_now] = colors_count(_teach_now);
            if (--_teach_left == 0) {
                tone_beep(_notes[_teach_now], 80);
            }
        }
        int match = colors_predict(f, &_status.distance);
        _status.match = match < 0 ? DASH_MATCH_NONE : (int8_t)match;
    }
    if (_status.match != last_match) {
        if (_status.match >= 0) {
            tone_beep(_notes[_status.match], 150);
        }
        last_match = _status.match;
    }
}

static void _handle_commands(void)
{
    mutex_lock(&_lock);
    bool text = _text_changed;
    _text_changed = false;
    uint16_t freq = _tone_freq, ms = _tone_ms;
    _tone_freq = 0;
    if (_teach_cmd == DASH_TEACH_SAMPLES) {
        _teach_now = _teach_slot;
        _teach_left = TEACH_SAMPLES;
        _teach_cmd = 0;
    }
    else if (_teach_cmd == DASH_TEACH_CLEAR) {
        colors_clear(_teach_slot);
        _status.counts[_teach_slot] = 0;
        _teach_cmd = 0;
    }
    else if (_teach_cmd == DASH_TEACH_CLEAR_ALL) {
        colors_reset();
        memset(_status.counts, 0, sizeof(_status.counts));
        _teach_cmd = 0;
    }
    mutex_unlock(&_lock);

    if (text) {
        printf("text: %s\n", _text);
        _draw_text();
    }
    if (freq) {
        tone_beep(freq, ms > 2000 ? 2000 : ms);
    }
}

static void _ble_init(void)
{
    int res = ble_gatts_count_cfg(_services);
    if (res == 0) {
        res = ble_gatts_add_svcs(_services);
    }
    if (res != 0) {
        printf("cannot add the GATT service: %d\n", res);
        return;
    }
    ble_svc_gap_device_name_set(CONFIG_NIMBLE_AUTOADV_DEVICE_NAME);
    ble_gatts_start();
    nimble_autoadv_set_gap_cb(_gap_event, NULL);
    nimble_autoadv_start(NULL);
    printf("advertising as %s\n", CONFIG_NIMBLE_AUTOADV_DEVICE_NAME);
}

int main(void)
{
    buttons_t buttons;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    gfx_clear(COLOR_BG);
    gfx_fill_rect(0, 0, gfx_width(), 26, COLOR_PANEL);
    gfx_text_centered(5, "BLE DASHBOARD", COLOR_ACCENT, COLOR_PANEL, 2);
    gfx_text_centered(gfx_height() - 12, "OPEN THE PAGE ON THE PHONE", COLOR_DIM, COLOR_BG, 1);
    puts("CLUE BLE Dashboard");

    _saul.temp = saul_reg_find_type(SAUL_SENSE_TEMP);
    _saul.hum = saul_reg_find_type(SAUL_SENSE_HUM);
    _saul.press = saul_reg_find_type(SAUL_SENSE_PRESS);
    _sensors.has_accel = accel_init(50) != 0;
    _apds_ok = apds99xx_init(&_apds, &apds99xx_params[0]) == APDS99XX_OK;
    _sensors.lux = NAN;
    if (!_apds_ok) {
        puts("no color sensor found");
    }
    gpio_init(LIGHT_PIN, GPIO_OUT);
    gpio_set(LIGHT_PIN);
    colors_reset();
    snprintf(_text, sizeof(_text), "HELLO FROM RIOT");
    _draw_text();
    _read_slow();
    _ble_init();
    _draw_connection();

    uint32_t last = ztimer_now(ZTIMER_MSEC);
    uint32_t next_slow = last + SLOW_MS;
    unsigned frames = 0;
    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);

        buttons_update(&buttons);
        _status.buttons = buttons.held & (BTN_A | BTN_B);
        if (_sensors.has_accel && !accel_read(_sensors.accel_g)) {
            memset(_sensors.accel_g, 0, sizeof(_sensors.accel_g));
        }
        _handle_commands();
        _color_reading();
        if ((int32_t)(now - next_slow) >= 0) {
            next_slow += SLOW_MS;
            _read_slow();
            _draw_connection();
        }
        if (_conn_changed) {
            _conn_changed = false;
            _draw_connection();
        }
        if (++frames % 3 == 0) {
            _draw_color();
        }

        mutex_lock(&_lock);
        dash_pack_sensors(_sensors_pkt, &_sensors);
        dash_pack_status(_status_pkt, &_status);
        mutex_unlock(&_lock);
        if (_conn != BLE_HS_CONN_HANDLE_NONE) {
            if (_notify_sensors) {
                _notify(_sensors_handle, _sensors_pkt, sizeof(_sensors_pkt));
            }
            if (_notify_status) {
                _notify(_status_handle, _status_pkt, sizeof(_status_pkt));
            }
        }
        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, FRAME_MS);
    }
    return 0;
}
