/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       CLUE BLE Motion: the orientation of the board over BLE, to
 *              turn a teapot in a web page
 *
 * A thread reads the accelerometer and the gyroscope 100 times per second
 * and updates the orientation filter of fusion.c, with the magnetometer in
 * 9 axis mode. The orientation is notified to the page of web/ 25 to 100
 * times per second, see protocol.h.
 *
 * Controls: A sets the heading back to zero, B switches between 6 and 9
 * axes. Keep the board still for 2 seconds at start-up, the gyroscope is
 * calibrated.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "mutex.h"
#include "nimble_autoadv.h"
#include "nimble_riot.h"
#include "phydat.h"
#include "saul_reg.h"
#include "services/gap/ble_svc_gap.h"
#include "thread.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"

#include "fusion.h"
#include "protocol.h"

/* the axes of the magnetometer in the axes of the accelerometer, not
   checked on the board yet */
#ifndef MAG_SIGN_X
#define MAG_SIGN_X      (1)
#endif
#ifndef MAG_SIGN_Y
#define MAG_SIGN_Y      (1)
#endif
#ifndef MAG_SIGN_Z
#define MAG_SIGN_Z      (1)
#endif

#define SENSOR_HZ       (100U)
#define SENSOR_MS       (1000U / SENSOR_HZ)
#define MAG_EVERY       (5U)        /* magnetometer at 20 Hz */
#define SCREEN_MS       (100U)
#define BUBBLE_X        (60)
#define BUBBLE_Y        (54)
#define BUBBLE_SIZE     (120)
#define DOT             (10)

#define COLOR_BG        GFX_RGB(14, 18, 34)
#define COLOR_PANEL     GFX_RGB(30, 40, 78)
#define COLOR_TEXT      GFX_RGB(230, 234, 255)
#define COLOR_DIM       GFX_RGB(120, 128, 168)
#define COLOR_ACCENT    GFX_RGB(90, 150, 255)
#define COLOR_DOT       GFX_RGB(255, 196, 70)
#define COLOR_GOOD      GFX_RGB(70, 220, 120)

/* c1ae01XX-5d8a-4a43-9a3c-6f5c2b1e7d90 */
#define MOTION_UUID(x)  BLE_UUID128_INIT(0x90, 0x7d, 0x1e, 0x2b, 0x5c, 0x6f, 0x3c, 0x9a, \
                                         0x43, 0x4a, 0x8a, 0x5d, (x), 0x01, 0xae, 0xc1)

static const ble_uuid128_t _uuid_service = MOTION_UUID(MOTION_UUID_SERVICE);
static const ble_uuid128_t _uuid_info = MOTION_UUID(MOTION_UUID_INFO);
static const ble_uuid128_t _uuid_motion = MOTION_UUID(MOTION_UUID_MOTION);
static const ble_uuid128_t _uuid_raw = MOTION_UUID(MOTION_UUID_RAW);
static const ble_uuid128_t _uuid_control = MOTION_UUID(MOTION_UUID_CONTROL);

static uint16_t _motion_handle, _raw_handle;
static volatile uint16_t _conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool _notify_motion, _notify_raw;
static volatile bool _conn_changed;

/* commands for the sensor thread, from the page or the buttons */
static volatile bool _do_recenter, _do_calibrate;
static volatile unsigned _axes = 6;
static volatile unsigned _notify_hz = 50;

/* the state of the sensor thread, copied with _lock */
static mutex_t _lock = MUTEX_INIT;
static float _q_rel[4] = { 1, 0, 0, 0 };
static bool _calibrating;
static bool _mag_ready;
static uint8_t _flags_buttons;

static saul_reg_t *_mag;
static char _sensor_stack[THREAD_STACKSIZE_DEFAULT + 1024];

static int _access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def _services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &_uuid_service.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &_uuid_info.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &_uuid_motion.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &_motion_handle,
            },
            {
                .uuid = &_uuid_raw.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &_raw_handle,
            },
            {
                .uuid = &_uuid_control.u,
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
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && ble_uuid_cmp(uuid, &_uuid_info.u) == 0) {
        uint8_t info[MOTION_INFO_LEN];
        motion_pack_info(info, SENSOR_HZ, _notify_hz, _mag != NULL, _axes);
        return os_mbuf_append(ctxt->om, info, sizeof(info)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || ble_uuid_cmp(uuid, &_uuid_control.u) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t buf[2];
    uint16_t len = 0;
    uint8_t cmd, value;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0 ||
        motion_parse_control(buf, len, &cmd, &value) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    switch (cmd) {
    case MOTION_CMD_RECENTER:
        _do_recenter = true;
        break;
    case MOTION_CMD_CALIBRATE:
        _do_calibrate = true;
        break;
    case MOTION_CMD_MODE:
        if (value == 9 && !_mag) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        _axes = value;
        break;
    case MOTION_CMD_RATE:
        _notify_hz = value;
        break;
    default:
        break;
    }
    return 0;
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
        /* 15 to 30 ms between the connection events, the phone decides */
        struct ble_gap_upd_params params = {
            .itvl_min = 12, .itvl_max = 24, .latency = 0, .supervision_timeout = 400,
        };
        ble_gap_update_params(_conn, &params);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            printf("connection interval %u.%02u ms\n", desc.conn_itvl * 5 / 4,
                   (desc.conn_itvl * 125) % 100);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        _conn = BLE_HS_CONN_HANDLE_NONE;
        _notify_motion = _notify_raw = false;
        _conn_changed = true;
        puts("disconnected");
        nimble_autoadv_start(NULL);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == _motion_handle) {
            _notify_motion = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == _raw_handle) {
            _notify_raw = event->subscribe.cur_notify;
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

/* From the axes of the sensor to the axes of the screen (x to the right, y
   to the top, z out of the screen): on the CLUE, the sensor is turned by
   180 degrees about the y axis. Measured: the board tilted by 45 degrees,
   screen facing up and to the viewer, reads y +0.72 g and z -0.69 g. */
static void _to_screen(float v[3])
{
    v[0] = -v[0];
    v[2] = -v[2];
}

/* the magnetic field in the axes of the accelerometer, centered on the
   extremes seen while the board turned (hard iron); false until the board
   turned enough */
static bool _read_mag(float m[3])
{
    static float min[3] = { INFINITY, INFINITY, INFINITY };
    static float max[3] = { -INFINITY, -INFINITY, -INFINITY };
    static const float sign[3] = { MAG_SIGN_X, MAG_SIGN_Y, MAG_SIGN_Z };
    phydat_t data;

    if (!_mag || saul_reg_read(_mag, &data) < 3) {
        return false;
    }
    float scale = powf(10.0f, data.scale);
    float radius = 0;
    for (unsigned i = 0; i < 3; i++) {
        float v = sign[i] * data.val[i] * scale;
        min[i] = fminf(min[i], v);
        max[i] = fmaxf(max[i], v);
        m[i] = v - (min[i] + max[i]) / 2;
        radius += m[i] * m[i];
    }
    radius = sqrtf(radius);
    /* a full turn about an axis spans twice the field on the other two */
    for (unsigned i = 0; i < 3; i++) {
        if (max[i] - min[i] < 1.5f * radius) {
            return false;
        }
    }
    return radius > 0;
}

static void *_sensor_thread(void *arg)
{
    (void)arg;
    fusion_t fusion;
    fusion_bias_t bias = { 0 };
    uint32_t last = ztimer_now(ZTIMER_MSEC);
    uint32_t last_us = ztimer_now(ZTIMER_USEC);
    unsigned count = 0;
    uint8_t seq = 0;
    float mag[3];
    bool mag_ok = false;

    fusion_init(&fusion);
    fusion_bias_start(&bias);

    while (1) {
        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, SENSOR_MS);
        uint32_t now_us = ztimer_now(ZTIMER_USEC);
        float dt = (now_us - last_us) / 1e6f;
        last_us = now_us;
        if (dt <= 0 || dt > 0.1f) {
            dt = SENSOR_MS / 1000.0f;
        }

        float g[3], dps[3];
        if (!accel_read(g) || !gyro_read(dps)) {
            continue;
        }
        _to_screen(g);
        _to_screen(dps);
        if (_do_calibrate) {
            _do_calibrate = false;
            fusion_bias_start(&bias);
        }
        if (bias.running && fusion_bias_add(&bias, dps, dt)) {
            printf("gyroscope bias %d %d %d (0.01 dps)\n", (int)(bias.bias[0] * 100),
                   (int)(bias.bias[1] * 100), (int)(bias.bias[2] * 100));
        }
        float w[3];
        for (unsigned i = 0; i < 3; i++) {
            w[i] = dps[i] - (bias.running ? 0 : bias.bias[i]);
        }
        if (_axes == 9 && count % MAG_EVERY == 0) {
            mag_ok = _read_mag(mag);
            _to_screen(mag);
        }
        bool use_mag = _axes == 9 && mag_ok;

        mutex_lock(&_lock);
        fusion_update(&fusion, w, g, use_mag ? mag : NULL, dt);
        if (_do_recenter) {
            _do_recenter = false;
            fusion_recenter(&fusion);
        }
        fusion_relative(&fusion, _q_rel);
        _calibrating = bias.running;
        _mag_ready = use_mag;
        motion_t m = {
            .seq = seq,
            .flags = _flags_buttons | (use_mag ? MOTION_FLAG_MAG : 0) |
                     (bias.running ? MOTION_FLAG_CALIB : 0),
        };
        memcpy(m.q, _q_rel, sizeof(m.q));
        mutex_unlock(&_lock);

        count++;
        if (count % (SENSOR_HZ / _notify_hz) == 0 && _conn != BLE_HS_CONN_HANDLE_NONE) {
            if (_notify_motion) {
                uint8_t pkt[MOTION_LEN];
                motion_pack(pkt, &m);
                _notify(_motion_handle, pkt, sizeof(pkt));
                seq++;
            }
            if (_notify_raw) {
                uint8_t pkt[MOTION_RAW_LEN];
                motion_pack_raw(pkt, g, dps);
                _notify(_raw_handle, pkt, sizeof(pkt));
            }
        }
    }
    return NULL;
}

/* --- screen -------------------------------------------------------------- */

static void _draw_static(void)
{
    int w = gfx_width();

    gfx_clear(COLOR_BG);
    gfx_fill_rect(0, 0, w, 26, COLOR_PANEL);
    gfx_text_centered(5, "BLE MOTION", COLOR_TEXT, COLOR_PANEL, 2);
    gfx_fill_rect(BUBBLE_X - 2, BUBBLE_Y - 2, BUBBLE_SIZE + 4, BUBBLE_SIZE + 4, COLOR_PANEL);
    gfx_fill_rect(BUBBLE_X, BUBBLE_Y, BUBBLE_SIZE, BUBBLE_SIZE, COLOR_BG);
    gfx_text_centered(gfx_height() - 12, "A: RECENTER   B: 6/9 AXES", COLOR_DIM, COLOR_BG, 1);
}

static void _draw_connection(void)
{
    gfx_fill_rect(0, 34, gfx_width(), 10, COLOR_BG);
    gfx_text_centered(34, _conn != BLE_HS_CONN_HANDLE_NONE ? "CONNECTED" :
                      "OPEN THE PAGE AND CONNECT",
                      _conn != BLE_HS_CONN_HANDLE_NONE ? COLOR_GOOD : COLOR_DIM, COLOR_BG, 1);
}

/* the bubble of a spirit level: the dot moves to the raised side, 45
   degrees at the edges. The pitch turns about the y axis of the screen,
   negative when the B side rises: it moves the dot sideways. The roll turns
   about the x axis, positive when the top rises: it moves the dot up. */
static void _draw_bubble(float roll, float pitch)
{
    static int old_x = -1, old_y = -1;
    const int half = (BUBBLE_SIZE - DOT) / 2;
    int x = BUBBLE_X + half - (int)lroundf(fmaxf(-1, fminf(1, pitch / 45)) * half);
    int y = BUBBLE_Y + half - (int)lroundf(fmaxf(-1, fminf(1, roll / 45)) * half);

    if (x == old_x && y == old_y) {
        return;
    }
    if (old_x >= 0) {
        gfx_fill_rect(old_x, old_y, DOT, DOT, COLOR_BG);
    }
    gfx_fill_rect(BUBBLE_X + BUBBLE_SIZE / 2, BUBBLE_Y, 1, BUBBLE_SIZE, COLOR_PANEL);
    gfx_fill_rect(BUBBLE_X, BUBBLE_Y + BUBBLE_SIZE / 2, BUBBLE_SIZE, 1, COLOR_PANEL);
    gfx_fill_rect(x, y, DOT, DOT, COLOR_DOT);
    old_x = x;
    old_y = y;
}

static void _draw_values(float roll, float pitch, float yaw, bool calibrating, bool mag)
{
    char text[40];
    int y = BUBBLE_Y + BUBBLE_SIZE + 10;

    snprintf(text, sizeof(text), "ROLL %4d  PITCH %4d  YAW %4d", (int)lroundf(roll),
             (int)lroundf(pitch), (int)lroundf(yaw));
    gfx_fill_rect(0, y, gfx_width(), 10, COLOR_BG);
    gfx_text_centered(y, text, COLOR_TEXT, COLOR_BG, 1);

    if (calibrating) {
        snprintf(text, sizeof(text), "KEEP STILL, CALIBRATING");
    }
    else if (_axes == 9) {
        snprintf(text, sizeof(text), "9 AXES, %s", mag ? "COMPASS ON" : "TURN THE BOARD AROUND");
    }
    else {
        snprintf(text, sizeof(text), "6 AXES  %u HZ", _notify_hz);
    }
    gfx_fill_rect(0, y + 16, gfx_width(), 10, COLOR_BG);
    gfx_text_centered(y + 16, text, calibrating ? COLOR_DOT : COLOR_ACCENT, COLOR_BG, 1);
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
    if (gfx_init() != 0) {
        puts("no display found");
    }
    puts("CLUE BLE Motion");
    _draw_static();

    if (accel_init(SENSOR_HZ) == 0 || !accel_has_gyro()) {
        gfx_text_centered(110, "NO MOTION SENSOR", COLOR_DOT, COLOR_BG, 2);
        puts("no accelerometer and gyroscope found");
        return 1;
    }
    _mag = saul_reg_find_type(SAUL_SENSE_MAG);
    if (!_mag) {
        puts("no magnetometer, 6 axes only");
    }
    thread_create(_sensor_stack, sizeof(_sensor_stack), THREAD_PRIORITY_MAIN - 1, 0,
                  _sensor_thread, NULL, "motion");
    _ble_init();
    _draw_connection();

    while (1) {
        buttons_update(&buttons);
        if (buttons.pressed & BTN_A) {
            _do_recenter = true;
        }
        if ((buttons.pressed & BTN_B) && _mag) {
            _axes = _axes == 6 ? 9 : 6;
        }
        if (_conn_changed) {
            _conn_changed = false;
            _draw_connection();
        }

        float q[4];
        mutex_lock(&_lock);
        _flags_buttons = ((buttons.held & BTN_A) ? MOTION_FLAG_A : 0) |
                         ((buttons.held & BTN_B) ? MOTION_FLAG_B : 0);
        memcpy(q, _q_rel, sizeof(q));
        bool calibrating = _calibrating;
        bool mag = _mag_ready;
        mutex_unlock(&_lock);

        float roll, pitch, yaw;
        fusion_euler(q, &roll, &pitch, &yaw);
        _draw_bubble(roll, pitch);
        _draw_values(roll, pitch, yaw, calibrating, mag);
        ztimer_sleep(ZTIMER_MSEC, SCREEN_MS);
    }
    return 0;
}
