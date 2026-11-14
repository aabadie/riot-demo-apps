/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Packets of the GATT service of the dashboard
 */

#include <math.h>

#include "protocol.h"

static void _u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int16_t _s16(float v, float factor)
{
    if (isnan(v)) {
        return DASH_INVALID_S16;
    }
    float r = roundf(v * factor);
    return (int16_t)(r < -32767 ? -32767 : (r > 32767 ? 32767 : r));
}

static uint16_t _unsigned(float v, float factor)
{
    if (isnan(v)) {
        return DASH_INVALID_U16;
    }
    float r = roundf(v * factor);
    return (uint16_t)(r < 0 ? 0 : (r > 65534 ? 65534 : r));
}

void dash_pack_sensors(uint8_t buf[DASH_SENSORS_LEN], const dash_sensors_t *s)
{
    for (unsigned i = 0; i < 3; i++) {
        _u16(buf + 2 * i, (uint16_t)_s16(s->has_accel ? s->accel_g[i] : NAN, 1000));
    }
    _u16(buf + 6, (uint16_t)_s16(s->temp_c, 100));
    _u16(buf + 8, _unsigned(s->hum, 100));
    _u16(buf + 10, _unsigned(s->press_hpa, 10));    /* 10 Pa */
    _u16(buf + 12, _unsigned(s->lux, 1));
    for (unsigned i = 0; i < 3; i++) {
        _u16(buf + 14 + 2 * i, s->has_rgb ? (s->rgb[i] < 0xffff ? s->rgb[i] : 0xfffe) :
                               DASH_INVALID_U16);
    }
}

void dash_pack_status(uint8_t buf[DASH_STATUS_LEN], const dash_status_t *s)
{
    float d = roundf(s->distance * 10);

    buf[0] = (uint8_t)s->match;
    buf[1] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
    buf[2] = (uint8_t)s->rssi;
    buf[3] = (uint8_t)(s->proximity > 255 ? 255 : s->proximity);
    for (unsigned i = 0; i < DASH_SLOTS; i++) {
        buf[4 + i] = (uint8_t)(s->counts[i] > 255 ? 255 : s->counts[i]);
    }
    buf[10] = s->buttons;
}

void dash_clean_text(char *out, const uint8_t *in, size_t len)
{
    size_t n = 0;

    for (size_t i = 0; i < len && n < DASH_TEXT_MAX; i++) {
        uint8_t c = in[i];
        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - 'a' + 'A');
        }
        else if (c == '\n') {
            c = ' ';
        }
        else if (c < ' ' || c > 'Z') {
            /* the font of the screen stops at Z */
            if (c >= 0x80) {
                /* skip the continuation bytes of a UTF-8 character */
                while (i + 1 < len && (in[i + 1] & 0xc0) == 0x80) {
                    i++;
                }
            }
            c = '?';
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
}
