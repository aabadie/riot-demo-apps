/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: the screen, a preview of the code
 *
 * The code is drawn black on white with its quiet zone, a phone reads it on
 * the screen too.
 */

#include "demo_gfx.h"
#include "qrcodegen.h"

#include "app.h"

#define COLOR_BG        GFX_RGB(16, 18, 28)
#define COLOR_TEXT      GFX_RGB(230, 235, 255)
#define COLOR_DIM       GFX_RGB(120, 125, 160)
#define COLOR_WHITE     GFX_RGB(255, 255, 255)
#define COLOR_BLACK     GFX_RGB(0, 0, 0)

#define TITLE_Y         (6)
#define CODE_Y          (24)
#define CODE_SIDE       (192)
#define STATUS_Y        (224)
/* modules of the quiet zone on the screen */
#define QUIET           (2)

static bool _ok;

void app_screen_init(void)
{
    _ok = gfx_init() == 0;
    if (!_ok) {
        return;
    }
    gfx_clear(COLOR_BG);
    gfx_text_centered(TITLE_Y, "QR PRINTER", COLOR_TEXT, COLOR_BG, 2);
}

static void _draw_code(const uint8_t *qr)
{
    static uint16_t line[CODE_SIDE];
    int size = qrcodegen_getSize(qr);
    int scale = CODE_SIDE / (size + 2 * QUIET);
    int side = (size + 2 * QUIET) * scale;
    int x0 = (gfx_width() - side) / 2;
    int y0 = CODE_Y + (CODE_SIDE - side) / 2;

    if (scale < 1) {
        return;
    }
    gfx_fill_rect(0, CODE_Y, gfx_width(), CODE_SIDE, COLOR_BG);
    gfx_fill_rect(x0, y0, side, QUIET * scale, COLOR_WHITE);
    gfx_fill_rect(x0, y0 + (size + QUIET) * scale, side, QUIET * scale, COLOR_WHITE);
    for (int my = 0; my < size; my++) {
        for (int mx = -QUIET; mx < size + QUIET; mx++) {
            uint16_t color = qrcodegen_getModule(qr, mx, my) ? COLOR_BLACK : COLOR_WHITE;
            for (int i = 0; i < scale; i++) {
                line[(mx + QUIET) * scale + i] = color;
            }
        }
        for (int i = 0; i < scale; i++) {
            gfx_pixels(x0, y0 + (my + QUIET) * scale + i, side, 1, line);
        }
    }
}

void app_screen_show(const uint8_t *qr, const char *status)
{
    if (!_ok) {
        return;
    }
    if (qr) {
        _draw_code(qr);
    }
    gfx_fill_rect(0, STATUS_Y, gfx_width(), GFX_CHAR_H * 2, COLOR_BG);
    gfx_text_centered(STATUS_Y, status, COLOR_DIM, COLOR_BG, 2);
}
