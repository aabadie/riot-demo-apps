/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Disco Digit Draw: draw a digit with a finger on the touch
 *              screen of the STM32F723E-DISCO, emlearn recognizes it
 *
 * The drawing is recognized while it is drawn and when the finger lifts,
 * with the network of Digit Oracle trained on MNIST. The digit and its
 * probability are shown on the screen and printed on the serial port.
 *
 * CLEAR, the blue user button, or a new touch 2 seconds after the last
 * stroke starts a new drawing.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "periph/gpio.h"
#include "touch_dev.h"
#include "ztimer.h"

#include "demo_gfx.h"

#include "digits.h"

#define FRAME_MS        (20U)       /* touch polling period */
#define CLASSIFY_MS     (200U)      /* recognition period while drawing */
#define NEW_DRAWING_MS  (2000U)     /* a touch after this starts again */
#define BRUSH           (1.6f)      /* brush radius in canvas pixels */
#define STEP            (0.3f)      /* brush steps along a stroke */

/* the canvas: the 28x28 pixels of an MNIST digit, 6 screen pixels each */
#define CELL            (6)
#define CANVAS_X        (4)
#define CANVAS_Y        (26)
#define CANVAS_W        (DIGITS_SIZE * CELL)

/* the panel on the right: the digit, its probability and all of them */
#define PANEL_X         (CANVAS_X + CANVAS_W + 6)
#define PANEL_W         (240 - PANEL_X - 2)
#define DIGIT_SCALE     (8)
#define DIGIT_Y         (30)
#define PERCENT_Y       (92)
#define BARS_Y          (114)
#define BAR_ROW         (8)
#define BAR_X           (PANEL_X + 8)
#define BAR_W           (PANEL_W - 10)

/* the clear button, under the canvas */
#define CLEAR_X         (CANVAS_X)
#define CLEAR_Y         (CANVAS_Y + CANVAS_W + 8)
#define CLEAR_W         (240 - 2 * CANVAS_X)
#define CLEAR_H         (240 - CLEAR_Y - 4)

#define COLOR_BG        GFX_RGB(10, 12, 30)
#define COLOR_FRAME     GFX_RGB(60, 70, 120)
#define COLOR_TEXT      GFX_RGB(220, 225, 255)
#define COLOR_DIM       GFX_RGB(110, 115, 150)
#define COLOR_ACCENT    GFX_RGB(255, 200, 70)
#define COLOR_BAR       GFX_RGB(70, 120, 230)
#define INK_R           (120)
#define INK_G           (255)
#define INK_B           (230)

static uint8_t _canvas[DIGITS_PIXELS];
static uint8_t _norm[DIGITS_PIXELS];
static bool _dirty[DIGITS_PIXELS];
static bool _empty = true;
static int _shown = -2;             /* digit on the panel, -1 for none */

static uint16_t _ink(uint8_t v)
{
    /* from the background to the ink color */
    unsigned r = 10 + (INK_R - 10) * v / 255;
    unsigned g = 12 + (INK_G - 12) * v / 255;
    unsigned b = 30 + (INK_B - 30) * v / 255;

    return GFX_RGB(r, g, b);
}

static void _paint(float px, float py)
{
    for (int y = (int)(py - BRUSH); y <= (int)(py + BRUSH); y++) {
        for (int x = (int)(px - BRUSH); x <= (int)(px + BRUSH); x++) {
            if (x < 0 || y < 0 || x >= DIGITS_SIZE || y >= DIGITS_SIZE) {
                continue;
            }
            float dx = x + 0.5f - px;
            float dy = y + 0.5f - py;
            float v = (BRUSH - sqrtf(dx * dx + dy * dy)) * 255.0f;
            if (v > 255.0f) {
                v = 255.0f;
            }
            unsigned i = y * DIGITS_SIZE + x;
            if (v > _canvas[i]) {
                _canvas[i] = (uint8_t)v;
                _dirty[i] = true;
                _empty = false;
            }
        }
    }
}

/* paint a stroke from (x0, y0) to (x1, y1), in canvas pixels */
static void _stroke(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    unsigned steps = (unsigned)(sqrtf(dx * dx + dy * dy) / STEP) + 1;

    for (unsigned i = 1; i <= steps; i++) {
        _paint(x0 + dx * i / steps, y0 + dy * i / steps);
    }
}

static void _draw_canvas(void)
{
    for (unsigned i = 0; i < DIGITS_PIXELS; i++) {
        if (_dirty[i]) {
            _dirty[i] = false;
            gfx_fill_rect(CANVAS_X + (i % DIGITS_SIZE) * CELL,
                          CANVAS_Y + (i / DIGITS_SIZE) * CELL, CELL, CELL, _ink(_canvas[i]));
        }
    }
}

static void _draw_result(int digit, const float proba[DIGITS_CLASSES])
{
    char text[8];

    gfx_fill_rect(PANEL_X, DIGIT_Y, PANEL_W, BARS_Y - DIGIT_Y - 4, COLOR_BG);
    if (digit >= 0) {
        text[0] = '0' + digit;
        text[1] = '\0';
        gfx_text(PANEL_X + (PANEL_W - gfx_text_width(text, DIGIT_SCALE)) / 2, DIGIT_Y,
                 text, COLOR_ACCENT, COLOR_BG, DIGIT_SCALE);
        snprintf(text, sizeof(text), "%d%%", (int)lroundf(proba[digit] * 100));
        gfx_text(PANEL_X + (PANEL_W - gfx_text_width(text, 2)) / 2, PERCENT_Y,
                 text, COLOR_TEXT, COLOR_BG, 2);
    }
    for (unsigned d = 0; d < DIGITS_CLASSES; d++) {
        int y = BARS_Y + d * BAR_ROW;
        int w = (digit >= 0) ? (int)lroundf(proba[d] * BAR_W) : 0;
        gfx_fill_rect(BAR_X, y, w, BAR_ROW - 2, (int)d == digit ? COLOR_ACCENT : COLOR_BAR);
        gfx_fill_rect(BAR_X + w, y, BAR_W - w, BAR_ROW - 2, COLOR_FRAME);
    }
}

static void _draw_static(void)
{
    gfx_clear(COLOR_BG);
    gfx_text_centered(5, "DRAW A DIGIT", COLOR_TEXT, COLOR_BG, 2);
    gfx_fill_rect(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_W + 2, COLOR_FRAME);
    gfx_fill_rect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_W, COLOR_BG);
    for (unsigned d = 0; d < DIGITS_CLASSES; d++) {
        char label[2] = { '0' + d, '\0' };
        gfx_text(PANEL_X, BARS_Y + d * BAR_ROW - 1, label, COLOR_DIM, COLOR_BG, 1);
    }
    gfx_fill_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, COLOR_FRAME);
    gfx_text(CLEAR_X + (CLEAR_W - gfx_text_width("CLEAR", 2)) / 2,
             CLEAR_Y + (CLEAR_H - 2 * GFX_CHAR_H) / 2 + 1, "CLEAR", COLOR_TEXT, COLOR_FRAME, 2);
}

static void _clear(void)
{
    memset(_canvas, 0, sizeof(_canvas));
    memset(_dirty, 0, sizeof(_dirty));
    _empty = true;
    _shown = -1;
    gfx_fill_rect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_W, COLOR_BG);
    _draw_result(-1, NULL);
}

/* recognize the drawing, update the panel, print the result when asked */
static void _recognize(bool print)
{
    float proba[DIGITS_CLASSES];

    if (_empty || digits_normalize(_canvas, _norm) < 0) {
        return;
    }
    int digit = digits_classify(_norm, proba);
    if (digit < 0) {
        return;
    }
    _draw_result(digit, proba);
    _shown = digit;
    if (print) {
        printf("digit %d (%d %%)\n", digit, (int)lroundf(proba[digit] * 100));
    }
}

static bool _in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

int main(void)
{
    if (gfx_init() < 0) {
        puts("no display");
        return 1;
    }
    touch_dev_reg_t *touch = touch_dev_reg_find_screen(0);
    if (touch == NULL) {
        puts("no touch screen");
        return 1;
    }
    gpio_init(BTN0_PIN, BTN0_MODE);

    _draw_static();
    _clear();
    puts("Disco Digit Draw: draw a digit on the screen");

    bool down = false;
    bool button = false;
    bool drew = false;          /* a stroke since the last recognition */
    float last_x = 0, last_y = 0;
    uint32_t up_since = 0;
    uint32_t last_classify = 0;

    while (1) {
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
        uint32_t now = ztimer_now(ZTIMER_MSEC);

        bool pressed = gpio_read(BTN0_PIN) != 0;
        if (pressed && !button) {
            _clear();
        }
        button = pressed;

        touch_t t;
        bool touching = touch_dev_touches(touch->dev, &t, 1) > 0;

        if (touching && _in(t.x, t.y, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_W)) {
            float x = (float)(t.x - CANVAS_X) / CELL;
            float y = (float)(t.y - CANVAS_Y) / CELL;
            if (!down) {
                if (!_empty && now - up_since > NEW_DRAWING_MS) {
                    _clear();
                }
                last_x = x;
                last_y = y;
                down = true;
            }
            _stroke(last_x, last_y, x, y);
            last_x = x;
            last_y = y;
            drew = true;
        }
        else if (touching && !down && _in(t.x, t.y, CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H)) {
            if (!_empty || _shown >= 0) {
                _clear();
            }
        }
        else if (down) {
            /* lifted, or slid out of the canvas */
            down = false;
            up_since = now;
        }

        _draw_canvas();
        if (drew && down && now - last_classify >= CLASSIFY_MS) {
            last_classify = now;
            _recognize(false);
        }
        if (drew && !down) {
            drew = false;
            _recognize(true);
        }
    }
    return 0;
}
