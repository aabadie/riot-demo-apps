/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Packets of the GATT service of CLUE BLE Motion
 */

#include <math.h>

#include "protocol.h"

static void _put16(uint8_t *buf, int32_t v)
{
    v = v > INT16_MAX ? INT16_MAX : (v < INT16_MIN ? INT16_MIN : v);
    buf[0] = (uint8_t)(v & 0xff);
    buf[1] = (uint8_t)((uint16_t)v >> 8);
}

void motion_pack(uint8_t *buf, const motion_t *m)
{
    buf[0] = m->seq;
    buf[1] = m->flags;
    for (unsigned i = 0; i < 4; i++) {
        _put16(&buf[2 + 2 * i], (int32_t)lroundf(m->q[i] * 16384.0f));
    }
}

void motion_pack_raw(uint8_t *buf, const float g[3], const float dps[3])
{
    for (unsigned i = 0; i < 3; i++) {
        _put16(&buf[2 * i], (int32_t)lroundf(g[i] * 1000.0f));
        _put16(&buf[6 + 2 * i], (int32_t)lroundf(dps[i] * 10.0f));
    }
}

void motion_pack_info(uint8_t *buf, unsigned sensor_hz, unsigned notify_hz,
                      bool has_mag, unsigned axes)
{
    buf[0] = MOTION_VERSION;
    buf[1] = (uint8_t)(sensor_hz & 0xff);
    buf[2] = (uint8_t)(sensor_hz >> 8);
    buf[3] = (uint8_t)notify_hz;
    buf[4] = has_mag ? MOTION_FEATURE_MAG : 0;
    buf[5] = (uint8_t)axes;
}

int motion_parse_control(const uint8_t *buf, size_t len, uint8_t *cmd, uint8_t *arg)
{
    if (len < 1 || len > 2) {
        return -1;
    }
    *cmd = buf[0];
    *arg = len == 2 ? buf[1] : 0;
    switch (*cmd) {
    case MOTION_CMD_RECENTER:
    case MOTION_CMD_CALIBRATE:
        return 0;
    case MOTION_CMD_MODE:
        return (*arg == 6 || *arg == 9) ? 0 : -1;
    case MOTION_CMD_RATE:
        return (*arg == 25 || *arg == 50 || *arg == 100) ? 0 : -1;
    default:
        return -1;
    }
}
