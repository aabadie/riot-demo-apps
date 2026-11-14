/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Color Sniffer: colors taught on the Adafruit CLUE and
 *              recognized with an emlearn Gaussian naive Bayes model
 *
 * Hold an object (a candy, a LEGO brick, a fruit) against the color sensor,
 * above the screen. Teach up to six colors, then the board recognizes them
 * and plays a note for each: a color piano.
 *
 * Controls:
 * - A (held): teach the color under the sensor to the selected slot
 * - B: select the next slot
 * - B (held for a second): forget the selected slot
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "apds99xx.h"
#include "apds99xx_params.h"
#include "periph/gpio.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "colors.h"

/* The two white LEDs next to the sensor. P0.10 is also an NFC pin of the
   nRF52840, they only light up when the NFC pins are used as GPIOs. */
#ifndef LIGHT_PIN
#define LIGHT_PIN       GPIO_PIN(0, 10)
#endif

#define FRAME_MS        (50U)
#define LONG_PRESS_MS   (1000U)
#define NEAR_PRX        (40U)       /* proximity reading of an object */

#define COLOR_BG        GFX_RGB(14, 14, 18)
#define COLOR_PANEL     GFX_RGB(34, 34, 44)
#define COLOR_TEXT      GFX_RGB(235, 235, 240)
#define COLOR_DIM       GFX_RGB(120, 120, 135)
#define COLOR_SELECT    GFX_RGB(255, 255, 255)
#define COLOR_MATCH     GFX_RGB(255, 200, 60)

static apds99xx_t _sensor;
static buttons_t _buttons;
static unsigned _selected;
static int _match = -2;
static uint16_t _slot_color[COLORS_SLOTS];
static uint32_t _slot_rgb[COLORS_SLOTS][3];
static unsigned _slot_n[COLORS_SLOTS];

static const uint16_t _notes[COLORS_SLOTS] = {
    NOTE_C5, NOTE_D5, NOTE_E5, NOTE_G5, NOTE_A5, NOTE_C6,
};

typedef struct {
    int x, y, w, h;
} rect_t;

static rect_t _slot_rect(unsigned slot)
{
    int w = gfx_width();
    int sw = (w - 16 - 2 * 6) / 3;
    int sh = 38;
    int x = 8 + (int)(slot % 3) * (sw + 6);
    int y = gfx_height() - 2 * (sh + 6) - 16 + (int)(slot / 3) * (sh + 6);

    return (rect_t){ x, y, sw, sh };
}

/* screen color of a reading, white balanced on its brightest channel */
static uint16_t _to_screen(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t max = r > g ? (r > b ? r : b) : (g > b ? g : b);

    if (max == 0) {
        return GFX_RGB(0, 0, 0);
    }
    return GFX_RGB(r * 255 / max, g * 255 / max, b * 255 / max);
}

static void _draw_slot(unsigned slot)
{
    rect_t r = _slot_rect(slot);
    uint16_t frame = slot == _selected ? COLOR_SELECT :
                     ((int)slot == _match ? COLOR_MATCH : COLOR_PANEL);
    char label[8];

    gfx_fill_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, frame);
    gfx_fill_rect(r.x, r.y, r.w, r.h,
                  colors_count(slot) ? _slot_color[slot] : COLOR_BG);
    snprintf(label, sizeof(label), "%u", slot + 1);
    gfx_text(r.x + 3, r.y + 3, label, COLOR_TEXT, COLOR_BG, 1);
    if (colors_count(slot)) {
        snprintf(label, sizeof(label), "%u", colors_count(slot));
        gfx_text(r.x + r.w - gfx_text_width(label, 1) - 3, r.y + r.h - 10,
                 label, COLOR_TEXT, COLOR_BG, 1);
    }
}

static void _draw_static(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(0, 0, gfx_width(), 24, COLOR_PANEL);
    gfx_text_centered(4, "COLOR SNIFFER", COLOR_TEXT, COLOR_PANEL, 2);
    gfx_text_centered(gfx_height() - 12, "HOLD A: TEACH  B: SLOT", COLOR_DIM,
                      COLOR_BG, 1);
    for (unsigned s = 0; s < COLORS_SLOTS; s++) {
        _draw_slot(s);
    }
}

static void _status(const char *text, const char *detail, uint16_t color)
{
    int x = 128;
    int w = gfx_width() - x - 4;

    gfx_fill_rect(x, 36, w, 70, COLOR_BG);
    gfx_text(x, 40, text, color, COLOR_BG, text[1] ? 2 : 5);
    if (detail) {
        gfx_text(x, 92, detail, COLOR_DIM, COLOR_BG, 1);
    }
}

static void _set_match(int match)
{
    if (match == _match) {
        return;
    }
    int old = _match;
    _match = match;
    if (old >= 0) {
        _draw_slot(old);
    }
    if (match >= 0) {
        _draw_slot(match);
        tone_beep(_notes[match], 150);
    }
}

static void _reading(void)
{
    apds99xx_rgb_t rgb;
    uint16_t clear, prx = 0;
    float f[COLORS_FEATURES];
    char text[24];

    if (apds99xx_read_rgb_raw(&_sensor, &rgb) != APDS99XX_OK ||
        apds99xx_read_als_raw(&_sensor, &clear) != APDS99XX_OK) {
        return;
    }
    apds99xx_read_prx_raw(&_sensor, &prx);

    /* live swatch */
    gfx_fill_rect(8, 34, 112, 74, _to_screen(rgb.red, rgb.green, rgb.blue));

    if (prx < NEAR_PRX) {
        _status("", "BRING AN OBJECT CLOSE", COLOR_DIM);
        _set_match(-1);
        return;
    }
    colors_features(rgb.red, rgb.green, rgb.blue, clear, f);

    if (_buttons.held & BTN_A) {
        colors_teach(_selected, f);
        /* keep the average reading of the slot for its swatch */
        _slot_rgb[_selected][0] += rgb.red;
        _slot_rgb[_selected][1] += rgb.green;
        _slot_rgb[_selected][2] += rgb.blue;
        _slot_n[_selected]++;
        unsigned n = _slot_n[_selected];
        _slot_color[_selected] = _to_screen(_slot_rgb[_selected][0] / n,
                                            _slot_rgb[_selected][1] / n,
                                            _slot_rgb[_selected][2] / n);
        _draw_slot(_selected);
        snprintf(text, sizeof(text), "%u SAMPLES", colors_count(_selected));
        _status("TEACHING", text, COLOR_TEXT);
        return;
    }

    float z = 0;
    int match = colors_predict(f, &z);
    if (match < 0) {
        snprintf(text, sizeof(text), "DISTANCE %d.%d", (int)z, (int)(z * 10) % 10);
        _status("?", text, COLOR_DIM);
    }
    else {
        char big[2] = { '1' + match, '\0' };
        snprintf(text, sizeof(text), "DISTANCE %d.%d", (int)z, (int)(z * 10) % 10);
        _status(big, text, COLOR_MATCH);
    }
    printf("C r=%u g=%u b=%u c=%u prx=%u slot=%d\n", rgb.red, rgb.green,
           rgb.blue, clear, prx, match);
    _set_match(match);
}

int main(void)
{
    uint32_t b_down = 0;
    bool b_long = false;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    gpio_init(LIGHT_PIN, GPIO_OUT);
    gpio_set(LIGHT_PIN);
    colors_reset();

    _draw_static();
    if (apds99xx_init(&_sensor, &apds99xx_params[0]) != APDS99XX_OK) {
        _status("", "NO COLOR SENSOR", COLOR_MATCH);
        puts("no color sensor found");
        return 1;
    }
    puts("Color Sniffer");

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);
        buttons_update(&_buttons);

        if (_buttons.pressed & BTN_B) {
            b_down = now;
            b_long = false;
        }
        if ((_buttons.held & BTN_B) && !b_long && now - b_down >= LONG_PRESS_MS) {
            b_long = true;
            colors_clear(_selected);
            _slot_rgb[_selected][0] = _slot_rgb[_selected][1] = 0;
            _slot_rgb[_selected][2] = 0;
            _slot_n[_selected] = 0;
            _draw_slot(_selected);
            tone_beep(NOTE_C4, 80);
        }
        if ((_buttons.released & BTN_B) && !b_long) {
            unsigned old = _selected;
            _selected = (_selected + 1) % COLORS_SLOTS;
            _draw_slot(old);
            _draw_slot(_selected);
        }

        if (apds99xx_data_ready_als(&_sensor) == APDS99XX_OK) {
            _reading();
        }
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
