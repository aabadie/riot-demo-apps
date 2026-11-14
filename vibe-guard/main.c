/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Vibe Guard: vibration anomaly detection learned on the device
 *
 * Put the board on a running machine (a fan, a washing machine, a 3D
 * printer). For 30 seconds it learns what normal vibrations look like, by
 * fitting a Gaussian mixture on their spectrum. Then it watches: the gauge
 * shows how unusual the vibration is, and an alarm sounds when it stays
 * unusual for a second.
 *
 * Controls: A learns again, B toggles the sound. On the PyBadge, START and
 * SELECT do the same.
 *
 * Each window is also printed on the serial port, as
 * "W <level> <band powers in dB>", to plot it on the PC.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "vibe.h"

#define FRAME_MS        (20U)
#define LEARN_S         (30U)
#define SETTLE_WINDOWS  (6U)    /* skip the vibrations of the button press */
#define ALARM_ON        (3U)    /* odd windows in a row to raise the alarm */
#define ALARM_OFF       (5U)    /* normal windows in a row to clear it */
#define RING            (256U)
#define DB_MIN          (-70.0f)
#define DB_MAX          (-5.0f)
#define LEVEL_MAX       (8.0f)

#define COLOR_BG        GFX_RGB(8, 10, 20)
#define COLOR_PANEL     GFX_RGB(22, 26, 46)
#define COLOR_TEXT      GFX_RGB(220, 225, 255)
#define COLOR_DIM       GFX_RGB(110, 115, 150)
#define COLOR_BAR       GFX_RGB(70, 140, 255)
#define COLOR_MARK      GFX_RGB(255, 255, 255)
#define COLOR_GOOD      GFX_RGB(60, 220, 110)
#define COLOR_WARN      GFX_RGB(255, 200, 60)
#define COLOR_BAD       GFX_RGB(240, 60, 60)
#define COLOR_ACCENT    GFX_RGB(255, 170, 60)

enum { STATE_NO_SENSOR, STATE_LEARN, STATE_WATCH };

typedef struct {
    int title_h;
    int spec_x, spec_y, spec_w, spec_h, bar_w;
    int gauge_y, gauge_h;
    int hist_y, hist_h, hist_w;
    unsigned text_scale;
} layout_t;

static layout_t _ui;
static float _ring[RING][3];
static volatile unsigned _widx;
static char _sampler_stack[THREAD_STACKSIZE_DEFAULT];

static int _state;
static unsigned _windows;       /* windows since the start of learning */
static unsigned _odd_run, _normal_run;
static bool _alarm;
static uint32_t _last_siren;
static int _hist_pos;
static buttons_t _buttons;

static const tone_note_t _siren[] = {
    { NOTE_A5, 120 }, { NOTE_E6, 120 }, { NOTE_A5, 120 }, { NOTE_E6, 120 },
};
static const tone_note_t _learned[] = {
    { NOTE_C6, 60 }, { NOTE_E6, 60 }, { NOTE_G6, 120 },
};

static void *_sampler(void *arg)
{
    (void)arg;
    uint32_t last = ztimer_now(ZTIMER_USEC);

    while (1) {
        float g[3];
        if (accel_read(g)) {
            unsigned i = _widx % RING;
            memcpy(_ring[i], g, sizeof(g));
            _widx++;
        }
        ztimer_periodic_wakeup(ZTIMER_USEC, &last, 1000000U / VIBE_RATE_HZ);
    }
    return NULL;
}

static void _layout(void)
{
    int w = gfx_width();
    int h = gfx_height();
    bool big = w >= 200;

    _ui.text_scale = big ? 2 : 1;
    _ui.title_h = big ? 22 : 12;
    _ui.spec_x = 4;
    _ui.spec_y = _ui.title_h + 4;
    _ui.spec_w = w - 8;
    _ui.spec_h = big ? 96 : 50;
    _ui.bar_w = _ui.spec_w / VIBE_BANDS;
    _ui.gauge_y = _ui.spec_y + _ui.spec_h + GFX_CHAR_H + 6;
    _ui.gauge_h = big ? 22 : 12;
    _ui.hist_y = _ui.gauge_y + _ui.gauge_h + 6;
    _ui.hist_h = h - _ui.hist_y - 3;
    _ui.hist_w = w - 8;
}

static void _title(const char *state, uint16_t color)
{
    int w = gfx_width();
    unsigned s = _ui.text_scale;

    gfx_fill_rect(0, 0, w, _ui.title_h, color == COLOR_BAD ? COLOR_BAD : COLOR_PANEL);
    gfx_text(4, (_ui.title_h - GFX_CHAR_H * s) / 2, "VIBE GUARD",
             color == COLOR_BAD ? COLOR_TEXT : COLOR_ACCENT,
             color == COLOR_BAD ? COLOR_BAD : COLOR_PANEL, s);
    int tw = gfx_text_width(state, 1);
    gfx_text(w - tw - 4, (_ui.title_h - GFX_CHAR_H) / 2, state,
             color == COLOR_BAD ? COLOR_TEXT : color,
             color == COLOR_BAD ? COLOR_BAD : COLOR_PANEL, 1);
}

static void _draw_static(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(_ui.spec_x, _ui.spec_y, _ui.spec_w, _ui.spec_h, COLOR_PANEL);
    for (int b = 0; b < VIBE_BANDS; b++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", (int)vibe_band_edges[b]);
        int x = _ui.spec_x + b * _ui.bar_w +
                (_ui.bar_w - gfx_text_width(label, 1)) / 2;
        gfx_text(x, _ui.spec_y + _ui.spec_h + 2, label, COLOR_DIM, COLOR_BG, 1);
    }
    gfx_fill_rect(4, _ui.hist_y, _ui.hist_w, _ui.hist_h, COLOR_PANEL);
    _hist_pos = 0;
}

static int _db_to_h(float db)
{
    float v = (db - DB_MIN) / (DB_MAX - DB_MIN);

    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    return (int)(v * (_ui.spec_h - 2));
}

static void _draw_spectrum(const float feat[VIBE_BANDS])
{
    const float *mean = vibe_learned_mean();

    for (int b = 0; b < VIBE_BANDS; b++) {
        int x = _ui.spec_x + b * _ui.bar_w + 2;
        int bw = _ui.bar_w - 4;
        int h = _db_to_h(feat[b]);
        int top = _ui.spec_y + 1;
        int bottom = _ui.spec_y + _ui.spec_h - 1;
        gfx_fill_rect(x, top, bw, bottom - top - h, COLOR_PANEL);
        gfx_fill_rect(x, bottom - h, bw, h, COLOR_BAR);
        if (vibe_trained()) {
            int m = _db_to_h(mean[b]);
            gfx_fill_rect(x - 1, bottom - m - 1, bw + 2, 2, COLOR_MARK);
        }
    }
}

static uint16_t _level_color(float level)
{
    if (level < VIBE_ALARM_LEVEL / 2) {
        return COLOR_GOOD;
    }
    return level < VIBE_ALARM_LEVEL ? COLOR_WARN : COLOR_BAD;
}

static void _draw_gauge(float fraction, uint16_t color, const char *text)
{
    int w = _ui.hist_w;
    int fill = (int)(fraction * w);

    fill = fill < 0 ? 0 : (fill > w ? w : fill);
    gfx_fill_rect(4, _ui.gauge_y, fill, _ui.gauge_h, color);
    gfx_fill_rect(4 + fill, _ui.gauge_y, w - fill, _ui.gauge_h, COLOR_PANEL);
    int ty = _ui.gauge_y + (_ui.gauge_h - GFX_CHAR_H) / 2;
    gfx_text(8, ty, text, COLOR_TEXT, fill > 8 + gfx_text_width(text, 1) ?
             color : COLOR_PANEL, 1);
    /* alarm threshold mark */
    if (_state == STATE_WATCH) {
        int x = 4 + (int)(VIBE_ALARM_LEVEL / LEVEL_MAX * w);
        gfx_fill_rect(x, _ui.gauge_y - 2, 2, _ui.gauge_h + 4, COLOR_MARK);
    }
}

static void _draw_history(float level)
{
    int x = 4 + _hist_pos;
    int h = (int)(level / LEVEL_MAX * (_ui.hist_h - 2));

    h = h < 1 ? 1 : (h > _ui.hist_h - 2 ? _ui.hist_h - 2 : h);
    gfx_fill_rect(x, _ui.hist_y + 1, 2, _ui.hist_h - 2 - h, COLOR_PANEL);
    gfx_fill_rect(x, _ui.hist_y + _ui.hist_h - 1 - h, 2, h, _level_color(level));
    _hist_pos = (_hist_pos + 2) % (_ui.hist_w - 2);
    /* sweep cursor */
    gfx_fill_rect(4 + _hist_pos, _ui.hist_y + 1, 2, _ui.hist_h - 2, COLOR_DIM);
}

static void _start_learning(void)
{
    vibe_learn_reset();
    _state = STATE_LEARN;
    _windows = 0;
    _alarm = false;
    _odd_run = _normal_run = 0;
    _draw_static();
    _title("LEARNING", COLOR_WARN);
    _draw_gauge(0, COLOR_WARN, "KEEP THE MACHINE RUNNING");
    puts("learning");
}

static void _window(void)
{
    static float window[VIBE_WINDOW][3];
    float feat[VIBE_BANDS];
    unsigned end = _widx;

    for (unsigned i = 0; i < VIBE_WINDOW; i++) {
        memcpy(window[i], _ring[(end - VIBE_WINDOW + i) % RING], sizeof(window[i]));
    }
    vibe_features(window, feat);
    _draw_spectrum(feat);
    _windows++;

    if (_state == STATE_LEARN) {
        if (_windows <= SETTLE_WINDOWS) {
            return;
        }
        unsigned total = LEARN_S * VIBE_RATE_HZ / VIBE_HOP;
        unsigned n = vibe_learn_add(feat);
        char text[32];
        snprintf(text, sizeof(text), "LEARNING %u/%u S", n * VIBE_HOP / VIBE_RATE_HZ,
                 LEARN_S);
        _draw_gauge((float)n / total, COLOR_WARN, text);
        if (n >= total || n >= VIBE_TRAIN_MAX) {
            vibe_learn_fit();
            _state = STATE_WATCH;
            _title("WATCHING", COLOR_GOOD);
            tone_effect(_learned, ARRAY_SIZE(_learned));
            printf("learned %u windows\n", n);
        }
        return;
    }

    float ll;
    float level = vibe_level(feat, &ll);
    char text[24];
    snprintf(text, sizeof(text), "LEVEL %d.%d", (int)level,
             (int)(level * 10) % 10);
    _draw_gauge(level / LEVEL_MAX, _level_color(level), text);
    _draw_history(level);

    printf("W %d.%02d", (int)level, (int)(level * 100) % 100);
    for (unsigned b = 0; b < VIBE_BANDS; b++) {
        printf(" %d", (int)feat[b]);
    }
    puts("");

    if (level >= VIBE_ALARM_LEVEL) {
        _odd_run++;
        _normal_run = 0;
    }
    else {
        _normal_run++;
        _odd_run = 0;
    }
    if (!_alarm && _odd_run >= ALARM_ON) {
        _alarm = true;
        _title("ALARM", COLOR_BAD);
        _last_siren = 0;
        puts("alarm");
    }
    else if (_alarm && _normal_run >= ALARM_OFF) {
        _alarm = false;
        _title("WATCHING", COLOR_GOOD);
        puts("back to normal");
    }
}

int main(void)
{
    unsigned done = 0;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    _layout();
    vibe_init();

    unsigned rate = accel_init(VIBE_RATE_HZ);
    if (rate == 0) {
        _state = STATE_NO_SENSOR;
        _draw_static();
        _title("NO SENSOR", COLOR_BAD);
        puts("no accelerometer found");
    }
    else {
        printf("Vibe Guard, accelerometer at %u Hz, sampled at %u Hz\n", rate,
               VIBE_RATE_HZ);
        thread_create(_sampler_stack, sizeof(_sampler_stack),
                      THREAD_PRIORITY_MAIN - 1, 0, _sampler, NULL, "sampler");
        _start_learning();
    }

    while (1) {
        buttons_update(&_buttons);
        if (_state != STATE_NO_SENSOR && (_buttons.pressed & (BTN_A | BTN_START))) {
            tone_beep(NOTE_C6, 30);
            _start_learning();
            done = _widx;
        }
        if (_buttons.pressed & (BTN_B | BTN_SELECT)) {
            tone_stop();
            tone_mute(!tone_is_muted());
        }

        if (_state != STATE_NO_SENSOR && _widx - done >= VIBE_HOP &&
            _widx >= VIBE_WINDOW) {
            done += VIBE_HOP;
            /* drop windows when the display is too slow to keep up */
            if (_widx - done >= VIBE_HOP) {
                done = _widx;
            }
            _window();
        }

        uint32_t now = ztimer_now(ZTIMER_MSEC);
        if (_alarm && now - _last_siren >= 2000) {
            _last_siren = now;
            tone_effect(_siren, ARRAY_SIZE(_siren));
        }
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
