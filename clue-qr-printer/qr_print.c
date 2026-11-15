/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: QR codes on the thermal printer
 */

#include <stdio.h>
#include <string.h>

#include "qr_print.h"

/* three lines of the default font */
#define CAPTION_MAX     (96U)

typedef struct {
    const uint8_t *qr;
    unsigned size;
    unsigned scale;
    unsigned x0;
} _layout_t;

bool qr_encode(const char *text, uint8_t *qr)
{
    static uint8_t tmp[QR_BUFFER_LEN];

    return qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW,
                                qrcodegen_VERSION_MIN, QR_VERSION_MAX,
                                qrcodegen_Mask_AUTO, true);
}

static void _row(void *arg, unsigned y, uint8_t *row, size_t len)
{
    const _layout_t *l = arg;
    unsigned my = y / l->scale;

    (void)len;
    for (unsigned mx = 0; mx < l->size; mx++) {
        if (!qrcodegen_getModule(l->qr, mx, my)) {
            continue;
        }
        unsigned x = l->x0 + mx * l->scale;
        for (unsigned i = 0; i < l->scale; i++, x++) {
            row[x / 8] |= 0x80 >> (x % 8);
        }
    }
}

void qr_print(adafruit_thermal_t *dev, const uint8_t *qr)
{
    _layout_t l = { .qr = qr, .size = qrcodegen_getSize(qr) };

    l.scale = ADAFRUIT_THERMAL_WIDTH / (l.size + 2 * QR_QUIET);
    if (l.scale > QR_SCALE_MAX) {
        l.scale = QR_SCALE_MAX;
    }
    l.x0 = (ADAFRUIT_THERMAL_WIDTH - l.size * l.scale) / 2;

    /* the quiet zone above and below is fed, not printed */
    adafruit_thermal_feed_rows(dev, QR_QUIET * l.scale);
    adafruit_thermal_bitmap(dev, l.x0 + l.size * l.scale, l.size * l.scale,
                            _row, &l);
    adafruit_thermal_feed_rows(dev, QR_QUIET * l.scale);
}

static bool _printable(const char *text)
{
    for (; *text; text++) {
        if ((*text < ' ') || (*text > '~')) {
            return false;
        }
    }
    return true;
}

void qr_print_caption(adafruit_thermal_t *dev, const char *text)
{
    size_t len = strlen(text);

    adafruit_thermal_justify(dev, ADAFRUIT_THERMAL_CENTER);
    if ((len <= CAPTION_MAX) && _printable(text)) {
        adafruit_thermal_print(dev, text);
    }
    else {
        char line[24];
        snprintf(line, sizeof(line), "(%u bytes)", (unsigned)len);
        adafruit_thermal_print(dev, line);
    }
    adafruit_thermal_print(dev, "\n");
    adafruit_thermal_justify(dev, ADAFRUIT_THERMAL_LEFT);
}
