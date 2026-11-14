/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       BTHome v2 advertising data and button events
 */

#include <math.h>

#include "bthome.h"

#define INFO_V2_PLAIN       (0x40)  /* version 2, not encrypted, regular */

#define ID_PACKET           (0x00)
#define ID_TEMP             (0x02)  /* sint16, 0.01 C */
#define ID_PRESS            (0x04)  /* uint24, 0.01 hPa */
#define ID_LUX              (0x05)  /* uint24, 0.01 lx */
#define ID_MOVING           (0x22)  /* binary */
#define ID_VIBRATION        (0x2c)  /* binary */
#define ID_HUM_1            (0x2e)  /* uint8, 1 % */
#define ID_BUTTON           (0x3a)  /* event */

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t pos;
    bool overflow;
} writer_t;

static void _put(writer_t *w, uint8_t id, uint32_t value, unsigned len)
{
    if (w->pos + 1 + len > w->size) {
        w->overflow = true;
        return;
    }
    w->buf[w->pos++] = id;
    for (unsigned i = 0; i < len; i++) {
        w->buf[w->pos++] = (uint8_t)(value >> (8 * i));
    }
}

static uint32_t _scaled(float value, float factor, float min, float max)
{
    float v = roundf(value * factor);

    v = v < min ? min : (v > max ? max : v);
    return (uint32_t)(int32_t)v;
}

size_t bthome_encode(uint8_t *buf, size_t size, const bthome_data_t *d)
{
    writer_t w = { buf, size, 0, false };

    if (size < 3) {
        return 0;
    }
    buf[w.pos++] = (uint8_t)(BTHOME_UUID & 0xff);
    buf[w.pos++] = (uint8_t)(BTHOME_UUID >> 8);
    buf[w.pos++] = INFO_V2_PLAIN;

    _put(&w, ID_PACKET, d->packet_id, 1);
    if (d->has_temp) {
        _put(&w, ID_TEMP, _scaled(d->temp_c, 100, -32768, 32767) & 0xffff, 2);
    }
    if (d->has_press) {
        _put(&w, ID_PRESS, _scaled(d->press_hpa, 100, 0, 0xffffff), 3);
    }
    if (d->has_lux) {
        _put(&w, ID_LUX, _scaled(d->lux, 100, 0, 0xffffff), 3);
    }
    if (d->has_motion) {
        _put(&w, ID_MOVING, d->moving, 1);
        _put(&w, ID_VIBRATION, d->vibration, 1);
    }
    if (d->has_hum) {
        _put(&w, ID_HUM_1, _scaled(d->hum, 1, 0, 100), 1);
    }
    /* one object per button, in order, so that the receiver numbers them */
    for (unsigned i = 0; i < BTHOME_BUTTONS; i++) {
        _put(&w, ID_BUTTON, d->button[i], 1);
    }
    return w.overflow ? 0 : w.pos;
}

uint8_t bthome_button_update(bthome_button_t *b, bool held, uint32_t now)
{
    uint8_t ev = BTHOME_EV_NONE;

    if (held && !b->held) {
        b->presses++;
        b->since = now;
        b->long_sent = false;
    }
    else if (!held && b->held) {
        b->since = now;
        if (b->long_sent) {
            b->presses = 0;
        }
    }
    else if (held && !b->long_sent && now - b->since >= BTHOME_LONG_MS) {
        /* a long press ends the sequence while the button is still held */
        b->long_sent = true;
        ev = BTHOME_EV_LONG;
    }
    else if (!held && b->presses && now - b->since >= BTHOME_GAP_MS) {
        ev = b->presses >= 3 ? BTHOME_EV_TRIPLE :
             (b->presses == 2 ? BTHOME_EV_DOUBLE : BTHOME_EV_PRESS);
        b->presses = 0;
    }
    b->held = held;
    return ev;
}
