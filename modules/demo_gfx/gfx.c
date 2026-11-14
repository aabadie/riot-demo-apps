/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     demo_gfx
 * @{
 *
 * @file
 * @brief       Minimal drawing helpers on top of disp_dev
 *
 * @}
 */

#include <errno.h>
#include <string.h>

#include "kernel_defines.h"
#include "disp_dev.h"
#include "demo_gfx.h"

/* Pixels sent to the display in one disp_dev_map call */
#ifndef GFX_BUF_PIXELS
#define GFX_BUF_PIXELS      (1024U)
#endif

/* Characters of the font: from ' ' to 'Z' */
#define FONT_FIRST          (' ')
#define FONT_LAST           ('Z')

extern const uint8_t demo_gfx_font5x7[][5];

static disp_dev_t *_dev;
static int _width;
static int _height;
static uint16_t _buf[GFX_BUF_PIXELS];

/* The lcd based drivers send the buffer bytes as they are, so the pixels must
   be stored in big endian order unless CONFIG_LCD_LE_MODE is set */
static inline uint16_t _px(uint16_t color)
{
    if (IS_ACTIVE(CONFIG_LCD_LE_MODE)) {
        return color;
    }
    return (uint16_t)((color >> 8) | (color << 8));
}

static void _map(int x1, int x2, int y1, int y2, const uint16_t *buf)
{
    const disp_dev_area_t area = {
        .x1 = (uint16_t)x1, .x2 = (uint16_t)x2, .y1 = (uint16_t)y1, .y2 = (uint16_t)y2,
    };
    disp_dev_map(_dev, &area, buf);
}

/* Clip a rectangle to the display, return false when nothing is left */
static bool _clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > _width) {
        *w = _width - *x;
    }
    if (*y + *h > _height) {
        *h = _height - *y;
    }
    return (*w > 0) && (*h > 0);
}

int gfx_init(void)
{
    disp_dev_reg_t *reg = disp_dev_reg_find_screen(0);
    if (reg == NULL) {
        return -ENODEV;
    }
    _dev = reg->dev;
    _width = disp_dev_width(_dev);
    _height = disp_dev_height(_dev);
    disp_dev_backlight_on();
    return 0;
}

int gfx_width(void)
{
    return _width;
}

int gfx_height(void)
{
    return _height;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!_dev || !_clip(&x, &y, &w, &h)) {
        return;
    }

    int rows_per_chunk = GFX_BUF_PIXELS / w;
    if (rows_per_chunk == 0) {
        /* wider than the buffer cannot happen with GFX_BUF_PIXELS >= width */
        rows_per_chunk = 1;
        w = GFX_BUF_PIXELS;
    }
    if (rows_per_chunk > h) {
        rows_per_chunk = h;
    }

    uint16_t px = _px(color);
    for (int i = 0; i < rows_per_chunk * w; i++) {
        _buf[i] = px;
    }

    for (int row = 0; row < h; row += rows_per_chunk) {
        int rows = (h - row < rows_per_chunk) ? (h - row) : rows_per_chunk;
        _map(x, x + w - 1, y + row, y + row + rows - 1, _buf);
    }
}

void gfx_clear(uint16_t color)
{
    gfx_fill_rect(0, 0, _width, _height, color);
}

static const uint8_t *_glyph(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < FONT_FIRST || c > FONT_LAST) {
        c = '?';
    }
    return demo_gfx_font5x7[c - FONT_FIRST];
}

/* Draw one character cell, cut in horizontal bands when it does not fit the
   buffer */
static void _char(int x, int y, char c, uint16_t fg, uint16_t bg, unsigned scale)
{
    const uint8_t *glyph = _glyph(c);
    const int cw = GFX_CHAR_W * scale;
    const int ch = GFX_CHAR_H * scale;
    const uint16_t pfg = _px(fg);
    const uint16_t pbg = _px(bg);

    /* skip the characters that are not fully on screen */
    if (x < 0 || y < 0 || x + cw > _width || y + ch > _height) {
        return;
    }

    int band = GFX_BUF_PIXELS / cw;
    if (band > ch) {
        band = ch;
    }

    for (int y0 = 0; y0 < ch; y0 += band) {
        int rows = (ch - y0 < band) ? (ch - y0) : band;
        for (int r = 0; r < rows; r++) {
            int fy = (y0 + r) / (int)scale;
            for (int col = 0; col < cw; col++) {
                int fx = col / (int)scale;
                bool on = (fx < 5) && (fy < 7) && (glyph[fx] & (1 << fy));
                _buf[r * cw + col] = on ? pfg : pbg;
            }
        }
        _map(x, x + cw - 1, y + y0, y + y0 + rows - 1, _buf);
    }
}

void gfx_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, unsigned scale)
{
    if (!_dev || scale == 0) {
        return;
    }
    for (; *text; text++) {
        _char(x, y, *text, fg, bg, scale);
        x += GFX_CHAR_W * scale;
    }
}

int gfx_text_width(const char *text, unsigned scale)
{
    /* the spacing column after the last character is not counted */
    size_t len = strlen(text);
    return len ? (int)(len * GFX_CHAR_W * scale - scale) : 0;
}

void gfx_text_centered(int y, const char *text, uint16_t fg, uint16_t bg, unsigned scale)
{
    gfx_text((_width - gfx_text_width(text, scale)) / 2, y, text, fg, bg, scale);
}

void gfx_pixels(int x, int y, int w, int h, const uint16_t *colors)
{
    if (!_dev || w <= 0 || h <= 0 || w * h > (int)GFX_BUF_PIXELS) {
        return;
    }
    if (x < 0 || y < 0 || x + w > _width || y + h > _height) {
        return;
    }
    for (int i = 0; i < w * h; i++) {
        _buf[i] = _px(colors[i]);
    }
    _map(x, x + w - 1, y, y + h - 1, _buf);
}

static uint16_t _sprite_color(const gfx_sprite_t *sprite, char key, uint16_t bg)
{
    for (unsigned i = 0; i < sprite->palette_len; i++) {
        if (sprite->palette[i].key == key) {
            return sprite->palette[i].color;
        }
    }
    return bg;
}

void gfx_sprite(int x, int y, const gfx_sprite_t *sprite, unsigned scale, uint16_t bg)
{
    const int w = sprite->width * scale;
    const int h = sprite->height * scale;

    if (!_dev || scale == 0) {
        return;
    }

    /* sprites partly out of the screen are drawn row by row, clipped */
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= _height) {
            continue;
        }
        int x1 = (x < 0) ? 0 : x;
        int x2 = (x + w > _width) ? _width : x + w;
        if (x2 <= x1 || (x2 - x1) > (int)GFX_BUF_PIXELS) {
            continue;
        }
        const char *line = sprite->rows[row / scale];
        for (int px = x1; px < x2; px++) {
            _buf[px - x1] = _px(_sprite_color(sprite, line[(px - x) / scale], bg));
        }
        _map(x1, x2 - 1, py, py, _buf);
    }
}
