/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    demo_gfx Minimal drawing helpers on top of disp_dev
 * @{
 *
 * @file
 * @brief       Filled rectangles, 5x7 text and character sprites on the
 *              first registered display, with clipping.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Build a RGB565 color from 8-bit components
 */
#define GFX_RGB(r, g, b)    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/**
 * @brief   Width of a character cell at scale 1 (5 pixels and 1 of spacing)
 */
#define GFX_CHAR_W          (6)

/**
 * @brief   Height of a character cell at scale 1 (7 pixels and 1 of spacing)
 */
#define GFX_CHAR_H          (8)

/**
 * @brief   Palette entry of a character sprite
 */
typedef struct {
    char key;           /**< character used in the sprite rows */
    uint16_t color;     /**< RGB565 color drawn for that character */
} gfx_palette_t;

/**
 * @brief   Sprite described by rows of characters
 *
 * Characters missing from the palette (for example '.') are drawn with the
 * background color.
 */
typedef struct {
    const char *const *rows;        /**< rows of the sprite */
    uint8_t width;                  /**< number of characters per row */
    uint8_t height;                 /**< number of rows */
    const gfx_palette_t *palette;   /**< palette of the sprite */
    uint8_t palette_len;            /**< number of palette entries */
} gfx_sprite_t;

/**
 * @brief   Find the display of screen 0 and turn the backlight on
 *
 * @return  0 on success, -ENODEV when no display is registered
 */
int gfx_init(void);

/**
 * @brief   Width of the display in pixels
 */
int gfx_width(void);

/**
 * @brief   Height of the display in pixels
 */
int gfx_height(void);

/**
 * @brief   Fill a rectangle, clipped to the display
 */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief   Fill the whole display
 */
void gfx_clear(uint16_t color);

/**
 * @brief   Draw a text with the 5x7 font, lowercase is shown as uppercase
 *
 * @param[in] x, y      top left corner
 * @param[in] text      text to draw
 * @param[in] fg        text color
 * @param[in] bg        background color of the character cells
 * @param[in] scale     size multiplier, from 1 to 4
 */
void gfx_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, unsigned scale);

/**
 * @brief   Width in pixels of a text drawn with @ref gfx_text
 */
int gfx_text_width(const char *text, unsigned scale);

/**
 * @brief   Draw a text horizontally centered on the display
 */
void gfx_text_centered(int y, const char *text, uint16_t fg, uint16_t bg, unsigned scale);

/**
 * @brief   Draw a block of RGB565 pixels, stored row by row
 *
 * @pre     w * h is at most 1024
 */
void gfx_pixels(int x, int y, int w, int h, const uint16_t *colors);

/**
 * @brief   Draw a sprite, each sprite character becomes a scale x scale block
 */
void gfx_sprite(int x, int y, const gfx_sprite_t *sprite, unsigned scale, uint16_t bg);

#ifdef __cplusplus
}
#endif

/** @} */
