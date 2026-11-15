/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       CLUE BTHome: the Adafruit CLUE as a BTHome v2 sensor beacon
 *
 * The board broadcasts its temperature, pressure, light and humidity in
 * BTHome v2 advertisements, with no connection and no pairing. Home
 * Assistant discovers it by itself. The buttons are sent as BTHome events
 * (press, double press, triple press, long press), and two binary sensors
 * come from the accelerometer: "moving", and "vibration", raised when the
 * vibration spectrum is unusual for the Gaussian mixture of Vibe Guard,
 * learned on the board during the first 30 seconds.
 *
 * Controls: A and B are the BTHome buttons; A and B held together for 2
 * seconds learn the normal vibrations again.
 *
 * Each advertisement is also printed on the serial port as
 * "B <packet id> <service data in hex>".
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "net/bluetil/ad.h"
#include "nimble_riot.h"
#include "phydat.h"
#include "saul_reg.h"
#include "thread.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"

#include "bthome.h"
#include "vibe.h"

#define FRAME_MS        (20U)
#define SENSORS_MS      (2000U)     /* new values, new packet id */
#define ADV_ITVL_MS     (100U)
#define COMBO_MS        (2000U)     /* A and B held: learn again */
#define LEARN_S         (30U)
#define SETTLE_WINDOWS  (6U)
#define ALARM_ON        (3U)
#define ALARM_OFF       (5U)
#define MOVING_G        (0.03f)     /* standard deviation of |a| when moving */
#define RING            (256U)

#define COLOR_BG        GFX_RGB(10, 12, 22)
#define COLOR_PANEL     GFX_RGB(26, 30, 52)
#define COLOR_TEXT      GFX_RGB(230, 235, 255)
#define COLOR_DIM       GFX_RGB(120, 125, 160)
#define COLOR_ACCENT    GFX_RGB(80, 200, 255)
#define COLOR_GOOD      GFX_RGB(60, 220, 110)
#define COLOR_WARN      GFX_RGB(255, 200, 60)
#define COLOR_BAD       GFX_RGB(240, 60, 60)

enum { VIBE_NONE, VIBE_LEARN, VIBE_WATCH };

static struct {
    saul_reg_t *temp;
    saul_reg_t *hum;
    saul_reg_t *press;
    saul_reg_t *als;
} _sensors;

static bthome_data_t _data;
static bthome_button_t _btn[BTHOME_BUTTONS];
static bthome_sched_t _sched;
static char _name[16];

static float _ring[RING][3];
static volatile unsigned _widx;
static char _sampler_stack[THREAD_STACKSIZE_DEFAULT];
static int _vibe_state;
static unsigned _windows, _odd_run, _normal_run;
static float _level;

static const char *_ev_names[] = { "-", "PRESS", "DOUBLE", "TRIPLE", "LONG" };

static void *_sampler(void *arg)
{
    (void)arg;
    uint32_t last = ztimer_now(ZTIMER_USEC);

    while (1) {
        float g[3];
        if (accel_read(g)) {
            memcpy(_ring[_widx % RING], g, sizeof(g));
            _widx++;
        }
        ztimer_periodic_wakeup(ZTIMER_USEC, &last, 1000000U / VIBE_RATE_HZ);
    }
    return NULL;
}

/* first value of a sensor in the given unit, false on error */
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

static void _find_sensors(void)
{
    _sensors.temp = saul_reg_find_type(SAUL_SENSE_TEMP);
    _sensors.hum = saul_reg_find_type(SAUL_SENSE_HUM);
    _sensors.press = saul_reg_find_type(SAUL_SENSE_PRESS);
    /* the APDS9960 gives two light entries, the clear channel and the color.
       It has no lux conversion: the clear channel is sent, in counts. */
    for (saul_reg_t *dev = saul_reg; dev; dev = dev->next) {
        phydat_t data;
        if (dev->driver->type == SAUL_SENSE_LIGHT && saul_reg_read(dev, &data) == 1 &&
            (!_sensors.als || data.unit == UNIT_LUX)) {
            _sensors.als = dev;
        }
    }
    printf("sensors: temp %s hum %s press %s light %s\n",
           _sensors.temp ? _sensors.temp->name : "-",
           _sensors.hum ? _sensors.hum->name : "-",
           _sensors.press ? _sensors.press->name : "-",
           _sensors.als ? _sensors.als->name : "-");
}

static void _read_sensors(void)
{
    uint8_t unit;
    float v;

    _data.has_temp = _read(_sensors.temp, &_data.temp_c, NULL);
    _data.has_hum = _read(_sensors.hum, &_data.hum, NULL);
    _data.has_press = _read(_sensors.press, &v, &unit);
    if (_data.has_press) {
        _data.press_hpa = unit == UNIT_PA ? v / 100.0f : v;
    }
    _data.has_lux = _read(_sensors.als, &_data.lux, NULL);
}

static void _advertise(void)
{
    uint8_t sd[BLE_HS_ADV_MAX_SZ];
    uint8_t buf[BLE_HS_ADV_MAX_SZ];
    bluetil_ad_t ad;
    size_t len = bthome_encode(sd, sizeof(sd) - 5, &_data);

    if (len == 0) {
        puts("advertisement too long");
        return;
    }
    bluetil_ad_init_with_flags(&ad, buf, sizeof(buf), BLUETIL_AD_FLAGS_DEFAULT);
    if (bluetil_ad_add(&ad, BLE_GAP_AD_SERVICE_DATA_UUID16, sd, len) != BLUETIL_AD_OK) {
        puts("advertisement too long");
        return;
    }
    int res = ble_gap_adv_set_data(ad.buf, ad.pos);
    if (res != 0) {
        printf("cannot set the advertising data: %d\n", res);
        return;
    }
    printf("B %u ", _data.packet_id);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", sd[i]);
    }
    puts("");
}

static void _ble_start(void)
{
    uint8_t addr[6] = { 0 };
    uint8_t buf[BLE_HS_ADV_MAX_SZ];
    bluetil_ad_t rsp;

    ble_hs_id_copy_addr(nimble_riot_own_addr_type, addr, NULL);
    /* NimBLE stores the address in reverse order */
    snprintf(_name, sizeof(_name), "CLUE-%02X%02X", addr[1], addr[0]);
    bluetil_ad_init(&rsp, buf, 0, sizeof(buf));
    bluetil_ad_add_name(&rsp, _name);
    ble_gap_adv_rsp_set_data(rsp.buf, rsp.pos);

    _advertise();
    /* not connectable, scannable for the name */
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MS),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MS),
    };
    int res = ble_gap_adv_start(nimble_riot_own_addr_type, NULL, BLE_HS_FOREVER,
                                &params, NULL, NULL);
    printf("advertising as %s: %d\n", _name, res);
}

static void _row(int row, const char *label, const char *value, uint16_t color)
{
    int y = 34 + row * 26;
    int w = gfx_width();

    gfx_fill_rect(0, y, w, 24, COLOR_BG);
    gfx_text(8, y + 4, label, COLOR_DIM, COLOR_BG, 2);
    gfx_text(w - gfx_text_width(value, 2) - 8, y + 4, value, color, COLOR_BG, 2);
}

static void _fmt(char *buf, size_t size, bool valid, float value, unsigned decimals,
                 const char *unit)
{
    if (!valid) {
        snprintf(buf, size, "-");
    }
    else if (decimals) {
        int t = (int)lroundf(value * 10);
        snprintf(buf, size, "%s%d.%d%s", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10,
                 (t < 0 ? -t : t) % 10, unit);
    }
    else {
        snprintf(buf, size, "%ld%s", lroundf(value), unit);
    }
}

static void _draw_values(void)
{
    char text[40];

    _fmt(text, sizeof(text), _data.has_temp, _data.temp_c, 1, "C");
    _row(0, "TEMP", text, COLOR_TEXT);
    _fmt(text, sizeof(text), _data.has_hum, _data.hum, 0, "%");
    _row(1, "HUMIDITY", text, COLOR_TEXT);
    _fmt(text, sizeof(text), _data.has_press, _data.press_hpa, 1, "");
    _row(2, "HPA", text, COLOR_TEXT);
    _fmt(text, sizeof(text), _data.has_lux, _data.lux, 0, "");
    _row(3, "LIGHT", text, COLOR_TEXT);
    _row(4, "MOVING", _data.moving ? "YES" : "NO", _data.moving ? COLOR_WARN : COLOR_TEXT);
    if (_vibe_state == VIBE_LEARN) {
        unsigned s = _windows * VIBE_HOP / VIBE_RATE_HZ;
        snprintf(text, sizeof(text), "LEARN %u", s < LEARN_S ? LEARN_S - s : 0);
        _row(5, "VIBRATION", text, COLOR_WARN);
    }
    else if (_vibe_state == VIBE_WATCH) {
        snprintf(text, sizeof(text), "%s %d.%d", _data.vibration ? "ODD" : "OK",
                 (int)_level, (int)(_level * 10) % 10);
        _row(5, "VIBRATION", text, _data.vibration ? COLOR_BAD : COLOR_GOOD);
    }
    else {
        _row(5, "VIBRATION", "-", COLOR_DIM);
    }
    snprintf(text, sizeof(text), "A %s  B %s", _ev_names[_data.button[0]],
             _ev_names[_data.button[1]]);
    gfx_fill_rect(0, 196, gfx_width(), 10, COLOR_BG);
    gfx_text_centered(196, text, COLOR_ACCENT, COLOR_BG, 1);
    snprintf(text, sizeof(text), "%s  PACKET %u", _name, _data.packet_id);
    gfx_fill_rect(0, 214, gfx_width(), 10, COLOR_BG);
    gfx_text_centered(214, text, COLOR_DIM, COLOR_BG, 1);
}

static void _start_learning(void)
{
    vibe_learn_reset();
    _vibe_state = VIBE_LEARN;
    _windows = 0;
    _odd_run = _normal_run = 0;
    _data.vibration = false;
    puts("learning the normal vibrations");
}

static void _window(void)
{
    static float window[VIBE_WINDOW][3];
    float feat[VIBE_BANDS];
    unsigned end = _widx;
    float sum = 0, sum2 = 0;

    for (unsigned i = 0; i < VIBE_WINDOW; i++) {
        const float *g = _ring[(end - VIBE_WINDOW + i) % RING];
        memcpy(window[i], g, sizeof(window[i]));
        float m = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
        sum += m;
        sum2 += m * m;
    }
    float mean = sum / VIBE_WINDOW;
    float var = sum2 / VIBE_WINDOW - mean * mean;
    _data.moving = sqrtf(var > 0 ? var : 0) > MOVING_G;

    vibe_features(window, feat);
    _windows++;
    if (_vibe_state == VIBE_LEARN) {
        if (_windows <= SETTLE_WINDOWS) {
            return;
        }
        unsigned n = vibe_learn_add(feat);
        if (n >= LEARN_S * VIBE_RATE_HZ / VIBE_HOP || n >= VIBE_TRAIN_MAX) {
            vibe_learn_fit();
            _vibe_state = VIBE_WATCH;
            printf("learned %u windows\n", n);
        }
        return;
    }
    _level = vibe_level(feat, NULL);
    if (_level >= VIBE_ALARM_LEVEL) {
        _odd_run++;
        _normal_run = 0;
    }
    else {
        _normal_run++;
        _odd_run = 0;
    }
    if (!_data.vibration && _odd_run >= ALARM_ON) {
        _data.vibration = true;
        puts("unusual vibration");
    }
    else if (_data.vibration && _normal_run >= ALARM_OFF) {
        _data.vibration = false;
        puts("vibration back to normal");
    }
}

int main(void)
{
    buttons_t buttons;
    unsigned done = 0;
    uint32_t combo_since = 0;
    bool combo = false;

    buttons_init();
    if (gfx_init() != 0) {
        puts("no display found");
    }
    gfx_clear(COLOR_BG);
    gfx_fill_rect(0, 0, gfx_width(), 26, COLOR_PANEL);
    gfx_text_centered(5, "BTHOME", COLOR_ACCENT, COLOR_PANEL, 2);
    puts("CLUE BTHome");

    _find_sensors();
    vibe_init();
    _vibe_state = VIBE_NONE;
    if (accel_init(VIBE_RATE_HZ)) {
        _data.has_motion = true;
        thread_create(_sampler_stack, sizeof(_sampler_stack), THREAD_PRIORITY_MAIN - 1,
                      0, _sampler, NULL, "sampler");
        _start_learning();
    }
    else {
        puts("no accelerometer found");
    }
    _read_sensors();
    _ble_start();
    _draw_values();

    uint32_t next_sensors = ztimer_now(ZTIMER_MSEC) + SENSORS_MS;
    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);
        bool changed = false;
        uint8_t events[BTHOME_BUTTONS] = { BTHOME_EV_NONE };

        buttons_update(&buttons);
        bool both = (buttons.held & (BTN_A | BTN_B)) == (BTN_A | BTN_B);
        if (both && !combo) {
            combo = true;
            combo_since = now;
        }
        if (combo) {
            if (both && combo_since && now - combo_since >= COMBO_MS && _data.has_motion) {
                combo_since = 0;
                _start_learning();
                _draw_values();
            }
            if (!(buttons.held & (BTN_A | BTN_B))) {
                combo = false;
                memset(_btn, 0, sizeof(_btn));
            }
        }
        else {
            const uint8_t mask[BTHOME_BUTTONS] = { BTN_A, BTN_B };
            for (unsigned i = 0; i < BTHOME_BUTTONS; i++) {
                events[i] = bthome_button_update(&_btn[i], buttons.held & mask[i], now);
                if (events[i] != BTHOME_EV_NONE) {
                    printf("button %c %s\n", 'A' + i, _ev_names[events[i]]);
                }
            }
        }

        if (_data.has_motion && _widx - done >= VIBE_HOP && _widx >= VIBE_WINDOW) {
            done += VIBE_HOP;
            if (_widx - done >= VIBE_HOP) {
                done = _widx;
            }
            bool moving = _data.moving, vibration = _data.vibration;
            _window();
            changed |= moving != _data.moving || vibration != _data.vibration;
        }

        if ((int32_t)(now - next_sensors) >= 0) {
            next_sensors += SENSORS_MS;
            _read_sensors();
            changed = true;
        }
        bool send = bthome_sched_update(&_sched, &_data, events, changed, now);
        if (send) {
            _data.packet_id++;
            _advertise();
        }
        if (send || changed) {
            _draw_values();
        }
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
