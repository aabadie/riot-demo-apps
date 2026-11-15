/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: the GATT service
 *
 * Service 5a7e0001-3c1d-4e8b-9f2a-7b6c4d1e0f90, characteristics
 * 5a7e00XX-...:
 *
 * - 02, write: the text to print, UTF-8, up to @ref APP_TEXT_MAX bytes
 * - 03, read and notify: the state of the printer (u8, APP_STATE_*) and
 *   the longest text (u16, little endian)
 */

#include <errno.h>
#include <stdio.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble_autoadv.h"
#include "nimble_riot.h"
#include "services/gap/ble_svc_gap.h"

#include "app.h"

#define UUID_SERVICE    (0x01)
#define UUID_TEXT       (0x02)
#define UUID_STATE      (0x03)
#define STATE_LEN       (3U)

/* errors of the application, from the first ATT application error */
#define ATT_ERR_BUSY    (0x80)  /* the printer prints another text */
#define ATT_ERR_TEXT    (0x81)  /* an empty text or one with a null byte */

/* 5a7e00XX-3c1d-4e8b-9f2a-7b6c4d1e0f90 */
#define QR_UUID(x)      BLE_UUID128_INIT(0x90, 0x0f, 0x1e, 0x4d, 0x6c, 0x7b, 0x2a, 0x9f, \
                                         0x8b, 0x4e, 0x1d, 0x3c, (x), 0x00, 0x7e, 0x5a)

static const ble_uuid128_t _uuid_service = QR_UUID(UUID_SERVICE);
static const ble_uuid128_t _uuid_text = QR_UUID(UUID_TEXT);
static const ble_uuid128_t _uuid_state = QR_UUID(UUID_STATE);

static uint16_t _state_handle;
static uint8_t _state = APP_STATE_READY;

static int _access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def _services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &_uuid_service.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &_uuid_text.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &_uuid_state.u,
                .access_cb = _access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &_state_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void _state_pkt(uint8_t *pkt)
{
    pkt[0] = _state;
    pkt[1] = APP_TEXT_MAX & 0xff;
    pkt[2] = APP_TEXT_MAX >> 8;
}

static int _access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    /* static: the host thread has a small stack, and it is the only caller */
    static char text[APP_TEXT_MAX];
    uint16_t len = 0;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t pkt[STATE_LEN];
        _state_pkt(pkt);
        return os_mbuf_append(ctxt->om, pkt, sizeof(pkt)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om, text, sizeof(text), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    switch (app_submit(text, len)) {
    case 0:
        printf("BLE: queued %u bytes\n", (unsigned)len);
        return 0;
    case -EBUSY:
        return ATT_ERR_BUSY;
    case -EMSGSIZE:
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    default:
        return ATT_ERR_TEXT;
    }
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
        puts("BLE: connected");
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        puts("BLE: disconnected");
        nimble_autoadv_start(NULL);
        break;
    default:
        break;
    }
    return 0;
}

void app_ble_state(uint8_t state)
{
    _state = state;
    /* notifies the subscribed clients, with the value read by _access() */
    ble_gatts_chr_updated(_state_handle);
}

void app_ble_init(void)
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
