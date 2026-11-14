/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Spell Caster: gesture recognition with an emlearn random forest
 *
 * Hold A and draw a gesture in the air with the board: a circle, a zigzag,
 * a flick of the wrist, a shake, or a thrust forward. Each gesture casts a
 * spell. The CLUE uses its accelerometer and gyroscope, the PyBadge its
 * accelerometer only.
 *
 * Record mode sends the gestures over the serial port, for
 * tools/record.py, to train a model on your own gestures with
 * tools/train.py. The default model is trained on synthetic gestures.
 *
 * Controls:
 * - A (held): capture a gesture
 * - B: enter record mode, then select the next gesture to record
 * - B (held for a second): back to casting
 * - PyBadge: START toggles the record mode, LEFT and RIGHT select the
 *   gesture, SELECT toggles the sound
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "random.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "gesture.h"

#define FRAME_MS        (20U)
#define LONG_PRESS_MS   (1000U)
#define MIN_PROBA       (0.45f)     /* below, the spell fizzles */

#define COLOR_BG        GFX_RGB(12, 6, 26)
#define COLOR_PANEL     GFX_RGB(32, 18, 60)
#define COLOR_TEXT      GFX_RGB(235, 225, 255)
#define COLOR_DIM       GFX_RGB(130, 110, 170)
#define COLOR_GOLD      GFX_RGB(255, 205, 80)
#define COLOR_REC       GFX_RGB(240, 60, 70)

typedef struct {
    const char *spell;
    uint16_t color;
} spell_t;

static const char *const _names[GESTURE_CLASSES] = GESTURE_NAMES;
static const spell_t _spells[GESTURE_CLASSES] = {
    { "SHIELD", GFX_RGB(80, 170, 255) },
    { "LIGHTNING", GFX_RGB(255, 240, 90) },
    { "FIREBALL", GFX_RGB(255, 110, 30) },
    { "FROST", GFX_RGB(170, 240, 255) },
    { "BLAST", GFX_RGB(220, 90, 255) },
};

static const tone_note_t _sfx[GESTURE_CLASSES][4] = {
    { { NOTE_C5, 60 }, { NOTE_E5, 60 }, { NOTE_G5, 60 }, { NOTE_C6, 160 } },
    { { NOTE_E6, 30 }, { NOTE_C4, 40 }, { NOTE_E6, 30 }, { NOTE_C4, 120 } },
    { { NOTE_C4, 50 }, { NOTE_E4, 50 }, { NOTE_G4, 50 }, { NOTE_C5, 200 } },
    { { NOTE_E6, 80 }, { NOTE_C6, 80 }, { NOTE_A5, 80 }, { NOTE_E5, 200 } },
    { { NOTE_G5, 40 }, { NOTE_G4, 40 }, { NOTE_C4, 60 }, { NOTE_C4, 160 } },
};
static const tone_note_t _fizzle[] = {
    { NOTE_E4, 80 }, { NOTE_C4, 200 },
};

static float _samples[GESTURE_MAX_SAMPLES * GESTURE_CHANNELS];
static int16_t _features[GESTURE_FEATURES];
static buttons_t _buttons;
static bool _record;
static int _rec_label;
static unsigned _rec_count[GESTURE_CLASSES];
static unsigned _casts;
static int _w, _h;
static unsigned _big;

/* drawing helpers */

static void _dot(int x, int y, int r, uint16_t color)
{
    gfx_fill_rect(x - r, y - r, 2 * r + 1, 2 * r + 1, color);
}

static void _line(int x0, int y0, int x1, int y1, int r, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        _dot(x0, y0, r, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void _circle(int cx, int cy, int r, int t, uint16_t color)
{
    int steps = 8 + r * 2;

    for (int i = 0; i < steps; i++) {
        float a = 2 * (float)M_PI * i / steps;
        _dot(cx + (int)(r * cosf(a)), cy + (int)(r * sinf(a)), t, color);
    }
}

static void _disc(int cx, int cy, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

/* screens */

static void _header(void)
{
    int th = _big ? 24 : 12;

    gfx_fill_rect(0, 0, _w, th, _record ? COLOR_REC : COLOR_PANEL);
    gfx_text_centered((th - GFX_CHAR_H * (_big ? 2 : 1)) / 2,
                      _record ? "RECORD MODE" : "SPELL CASTER", COLOR_TEXT,
                      _record ? COLOR_REC : COLOR_PANEL, _big ? 2 : 1);
}

static void _home(const char *line1, const char *line2)
{
    char text[32];

    gfx_clear(COLOR_BG);
    _header();
    int y = _h / 2 - (_big ? 30 : 18);
    if (_record) {
        gfx_text_centered(y, _names[_rec_label], COLOR_GOLD, COLOR_BG,
                          _big ? 3 : 2);
        snprintf(text, sizeof(text), "RECORDED %u", _rec_count[_rec_label]);
        gfx_text_centered(y + (_big ? 34 : 22), text, COLOR_DIM, COLOR_BG, 1);
    }
    else {
        gfx_text_centered(y, "HOLD A", COLOR_GOLD, COLOR_BG, _big ? 3 : 2);
        gfx_text_centered(y + (_big ? 34 : 22), "AND DRAW A SPELL", COLOR_DIM,
                          COLOR_BG, 1);
    }
    if (line1) {
        gfx_text_centered(_h - (_big ? 40 : 26), line1, COLOR_TEXT, COLOR_BG, 1);
    }
    if (line2) {
        gfx_text_centered(_h - (_big ? 26 : 14), line2, COLOR_DIM, COLOR_BG, 1);
    }
}

static void _home_default(void)
{
    if (_record) {
        _home("HOLD A AND DO THE GESTURE", "B: NEXT  HOLD B: CAST");
    }
    else {
        char text[32];
        snprintf(text, sizeof(text), "SPELLS CAST: %u", _casts);
        _home(text, "B: RECORD MODE");
    }
}

/* spell animations, about a second each */

static void _anim_shield(uint16_t color)
{
    int cx = _w / 2, cy = _h / 2 + 6;
    int rmax = (_h < _w ? _h : _w) / 2 - 10;

    for (int r = 6; r < rmax; r += 4) {
        _circle(cx, cy, r, 1, color);
        _circle(cx, cy, r - 6 > 0 ? r - 6 : 0, 1, COLOR_PANEL);
        ztimer_sleep(ZTIMER_MSEC, 30);
    }
    _circle(cx, cy, rmax - 4, 2, color);
}

static void _anim_lightning(uint16_t color)
{
    for (int flash = 0; flash < 2; flash++) {
        gfx_clear(COLOR_TEXT);
        ztimer_sleep(ZTIMER_MSEC, 40);
        gfx_clear(COLOR_BG);
        int x = _w / 2 + (int)random_uint32_range(0, 40) - 20;
        int y = 0;
        while (y < _h) {
            int nx = x + (int)random_uint32_range(0, _w / 4) - _w / 8;
            int ny = y + _h / 7;
            _line(x, y, nx, ny, _big ? 2 : 1, color);
            x = nx;
            y = ny;
        }
        ztimer_sleep(ZTIMER_MSEC, 200);
    }
}

static void _anim_fireball(uint16_t color)
{
    int cx = _w / 2;
    int rmax = _w / 6;

    for (int i = 0; i <= 12; i++) {
        int cy = _h - 10 - i * (_h - 40) / 12;
        int r = 4 + rmax * i / 12;
        _disc(cx, cy + 8, r, COLOR_BG);
        _disc(cx, cy, r, color);
        _disc(cx, cy, r / 2, COLOR_GOLD);
        ztimer_sleep(ZTIMER_MSEC, 40);
    }
}

static void _anim_frost(uint16_t color)
{
    int x[40], y[40];

    for (int i = 0; i < 40; i++) {
        x[i] = random_uint32_range(0, _w);
        y[i] = -(int)random_uint32_range(0, _h);
    }
    for (int frame = 0; frame < 30; frame++) {
        for (int i = 0; i < 40; i++) {
            _dot(x[i], y[i], 1, COLOR_BG);
            y[i] += 4 + i % 4;
            x[i] += (frame + i) % 3 - 1;
            if (y[i] >= 0 && y[i] < _h) {
                _dot(x[i], y[i], 1, i % 3 ? color : COLOR_TEXT);
            }
        }
        ztimer_sleep(ZTIMER_MSEC, 30);
    }
}

static void _anim_blast(uint16_t color)
{
    int cx = _w / 2, cy = _h / 2;
    int len = (_w > _h ? _w : _h) / 2;

    for (int i = 0; i < 16; i++) {
        float a = 2 * (float)M_PI * i / 16;
        _line(cx, cy, cx + (int)(len * cosf(a)), cy + (int)(len * sinf(a)),
              _big ? 2 : 1, i % 2 ? color : COLOR_GOLD);
        ztimer_sleep(ZTIMER_MSEC, 25);
    }
    _disc(cx, cy, len / 4, COLOR_TEXT);
}

static void _cast(int spell, float proba)
{
    char text[32];

    gfx_clear(COLOR_BG);
    tone_effect(_sfx[spell], 4);
    switch (spell) {
    case 0: _anim_shield(_spells[spell].color); break;
    case 1: _anim_lightning(_spells[spell].color); break;
    case 2: _anim_fireball(_spells[spell].color); break;
    case 3: _anim_frost(_spells[spell].color); break;
    default: _anim_blast(_spells[spell].color); break;
    }
    int y = _big ? 8 : 4;
    gfx_text_centered(y, _spells[spell].spell, _spells[spell].color, COLOR_BG,
                      _big ? 3 : 2);
    snprintf(text, sizeof(text), "%s %d%%", _names[spell], (int)(proba * 100 + 0.5f));
    gfx_text_centered(_h - 12, text, COLOR_DIM, COLOR_BG, 1);
    ztimer_sleep(ZTIMER_MSEC, 1200);
}

/* capture while A is held, return the number of samples */
static unsigned _capture(void)
{
    unsigned n = 0;
    uint32_t last = ztimer_now(ZTIMER_USEC);
    int bar_y = _h - (_big ? 60 : 36);

    gfx_fill_rect(0, bar_y, _w, 6, COLOR_BG);
    while ((buttons_read() & BTN_A) && n < GESTURE_MAX_SAMPLES) {
        float *s = &_samples[n * GESTURE_CHANNELS];
        if (!accel_read(s)) {
            break;
        }
        if (!gyro_read(&s[3])) {
            s[3] = s[4] = s[5] = 0;
        }
        n++;
        if (n % 5 == 0) {
            /* time bar, also tells that the capture runs */
            int w = n * _w / GESTURE_MAX_SAMPLES;
            gfx_fill_rect(0, bar_y, w, 6, _record ? COLOR_REC : COLOR_GOLD);
        }
        ztimer_periodic_wakeup(ZTIMER_USEC, &last, 1000000U / GESTURE_RATE_HZ);
    }
    return n;
}

static void _send_recording(unsigned n)
{
    printf("R %s %u", _names[_rec_label], n);
    for (unsigned i = 0; i < n; i++) {
        const float *s = &_samples[i * GESTURE_CHANNELS];
        /* the units of the drivers: milli-g and tenths of dps */
        printf(" %d %d %d %d %d %d", (int)lroundf(s[0] * 1000),
               (int)lroundf(s[1] * 1000), (int)lroundf(s[2] * 1000),
               (int)lroundf(s[3] * 10), (int)lroundf(s[4] * 10),
               (int)lroundf(s[5] * 10));
    }
    puts("");
}

static void _gesture(unsigned n)
{
    float proba[GESTURE_CLASSES];

    if (n < GESTURE_MIN_SAMPLES) {
        _home_default();
        return;
    }
    if (_record) {
        _send_recording(n);
        _rec_count[_rec_label]++;
        tone_beep(NOTE_A5, 40);
        _home_default();
        return;
    }

    gesture_features(_samples, n, _features);
    int spell = gesture_classify(_features, proba);
    if (spell < 0 || proba[spell] < MIN_PROBA) {
        printf("G FIZZLE %d\n", spell < 0 ? 0 : (int)(proba[spell] * 100));
        tone_effect(_fizzle, ARRAY_SIZE(_fizzle));
        _home("THE SPELL FIZZLED", "TRY AGAIN");
        return;
    }
    printf("G %s %d\n", _names[spell], (int)(proba[spell] * 100 + 0.5f));
    _casts++;
    _cast(spell, proba[spell]);
    _home_default();
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
    _w = gfx_width();
    _h = gfx_height();
    _big = _w >= 200;
    random_init(ztimer_now(ZTIMER_USEC));

    unsigned rate = accel_init(GESTURE_RATE_HZ);
    if (rate == 0) {
        gfx_clear(COLOR_BG);
        _header();
        gfx_text_centered(_h / 2, "NO MOTION SENSOR", COLOR_REC, COLOR_BG, 1);
        puts("no motion sensor found");
        return 1;
    }
    printf("Spell Caster, %s, %u Hz\n", accel_has_gyro() ?
           "accelerometer and gyroscope" : "accelerometer", rate);
    _home_default();

    while (1) {
        buttons_update(&_buttons);
        uint32_t now = ztimer_now(ZTIMER_MSEC);

        if (_buttons.pressed & BTN_SELECT) {
            tone_mute(!tone_is_muted());
        }
        if (_buttons.pressed & BTN_START) {
            _record = !_record;
            _home_default();
        }
        if (_record && (_buttons.pressed & (BTN_LEFT | BTN_RIGHT))) {
            int step = (_buttons.pressed & BTN_LEFT) ? GESTURE_CLASSES - 1 : 1;
            _rec_label = (_rec_label + step) % GESTURE_CLASSES;
            _home_default();
        }

        /* B: short press next / record mode, long press back to casting */
        if (_buttons.pressed & BTN_B) {
            b_down = now;
            b_long = false;
        }
        if ((_buttons.held & BTN_B) && !b_long && _record &&
            now - b_down >= LONG_PRESS_MS) {
            b_long = true;
            _record = false;
            tone_beep(NOTE_C5, 40);
            _home_default();
        }
        if ((_buttons.released & BTN_B) && !b_long) {
            if (_record) {
                _rec_label = (_rec_label + 1) % GESTURE_CLASSES;
            }
            _record = true;
            _home_default();
        }

        if (_buttons.pressed & BTN_A) {
            _gesture(_capture());
            buttons_update(&_buttons);
        }
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
