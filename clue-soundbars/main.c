/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Sound Bars: live audio spectrum of the CLUE microphone with lvgl
 *
 * The microphone is sampled with the PDM peripheral at 16 kHz. A thread
 * computes the level and a 16 band spectrum of each 16 ms frame with the
 * emlearn FFT, lvgl shows them. Button A switches between the spectrum, a
 * level meter and the level history; button B changes the gain.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "periph/pdm.h"
#include "thread.h"
#include "thread_flags.h"
#include "ztimer.h"

#include "lvgl/lvgl.h"
#include "lvgl_riot.h"

#include "eml_fft.h"

#include "demo_board.h"

#define SAMPLE_RATE     (16000U)
#define FRAME_LEN       (256U)
#define NUM_BANDS       (16U)
#define BAND_FMIN       (125.0f)
#define BAND_FMAX       (7600.0f)
#define DB_MIN          (-85.0f)    /* bottom of the bars in dBFS */
#define DB_MAX          (-20.0f)    /* top of the bars in dBFS */
#define SKIP_FRAMES     (20U)       /* microphone start-up, about 300 ms */

#define REFRESH_MS      (33U)
#define PEAK_HOLD       (15U)       /* refreshes */
#define HISTORY_POINTS  (90U)

#define FLAG_FRAME      (1U << 0)

static const int8_t _gains[] = { 0, 10, 20 };

/* --- audio side ---------------------------------------------------------- */

static int16_t _frames[2][FRAME_LEN];
static volatile unsigned _fill_frame;
static volatile unsigned _fill_pos;
static volatile int _ready_frame = -1;
static kernel_pid_t _dsp_pid = KERNEL_PID_UNDEF;
static unsigned _gain_idx = 1;
static unsigned _skip;

static float _re[FRAME_LEN];
static float _im[FRAME_LEN];
static float _fft_sin[FRAME_LEN / 2];
static float _fft_cos[FRAME_LEN / 2];
static const EmlFFT _fft = { FRAME_LEN / 2, _fft_sin, _fft_cos };
static float _window[FRAME_LEN];
static uint16_t _band_bins[NUM_BANDS + 1];

/* Results, written by the DSP thread, read by the lvgl timer */
static volatile float _level_db = DB_MIN;
static volatile float _band_db[NUM_BANDS];

static char _dsp_stack[THREAD_STACKSIZE_DEFAULT + 512];

static void _pdm_cb(void *arg, int16_t *buf)
{
    (void)arg;
    unsigned pos = _fill_pos;
    unsigned frame = _fill_frame;

    for (unsigned i = 0; i < PDM_BUF_SIZE; i++) {
        _frames[frame][pos++] = buf[i];
        if (pos == FRAME_LEN) {
            _ready_frame = (int)frame;
            frame ^= 1;
            pos = 0;
            thread_flags_set(thread_get(_dsp_pid), FLAG_FRAME);
        }
    }
    _fill_pos = pos;
    _fill_frame = frame;
}

static void _mic_start(void)
{
    pdm_stop();
    int32_t rate = pdm_init(PDM_MODE_MONO, SAMPLE_RATE, _gains[_gain_idx], _pdm_cb, NULL);
    if (rate < 0) {
        printf("PDM init failed: %ld\n", (long)rate);
        return;
    }
    printf("PDM running at %ld Hz, gain %d dB\n", (long)rate, _gains[_gain_idx]);
    _skip = SKIP_FRAMES;
    pdm_start();
}

static void _dsp_init(void)
{
    eml_fft_fill(_fft, FRAME_LEN);
    for (unsigned i = 0; i < FRAME_LEN; i++) {
        _window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (FRAME_LEN - 1));
    }
    /* logarithmically spaced bands */
    const float bin_hz = (float)SAMPLE_RATE / FRAME_LEN;
    for (unsigned b = 0; b <= NUM_BANDS; b++) {
        float f = BAND_FMIN * powf(BAND_FMAX / BAND_FMIN, (float)b / NUM_BANDS);
        unsigned bin = (unsigned)lroundf(f / bin_hz);
        if (b > 0 && bin <= _band_bins[b - 1]) {
            bin = _band_bins[b - 1] + 1;
        }
        _band_bins[b] = (uint16_t)bin;
    }
    for (unsigned b = 0; b < NUM_BANDS; b++) {
        _band_db[b] = DB_MIN;
    }
}

static void _process(const int16_t *samples)
{
    static float prev_x, prev_y;
    double energy = 0;

    /* DC blocking high-pass filter, then the window */
    for (unsigned i = 0; i < FRAME_LEN; i++) {
        float x = samples[i];
        float y = x - prev_x + 0.995f * prev_y;
        prev_x = x;
        prev_y = y;
        energy += (double)y * y;
        _re[i] = y * _window[i];
        _im[i] = 0.0f;
    }

    float rms = sqrtf((float)(energy / FRAME_LEN));
    _level_db = 20.0f * log10f(rms / 32768.0f + 1e-9f);

    eml_fft_forward(_fft, _re, _im, FRAME_LEN);

    /* a full scale sine gives a bin magnitude of 32768 * N / 4 with the Hann
       window, use it as the 0 dBFS reference */
    const float ref = 32768.0f * FRAME_LEN / 4.0f;
    for (unsigned b = 0; b < NUM_BANDS; b++) {
        float power = 0.0f;
        for (unsigned k = _band_bins[b]; k < _band_bins[b + 1]; k++) {
            power += _re[k] * _re[k] + _im[k] * _im[k];
        }
        _band_db[b] = 10.0f * log10f(power / (ref * ref) + 1e-12f);
    }
}

static void *_dsp_thread(void *arg)
{
    (void)arg;
    while (1) {
        thread_flags_wait_any(FLAG_FRAME);
        int frame = _ready_frame;
        if (frame < 0) {
            continue;
        }
        if (_skip) {
            _skip--;
            continue;
        }
        _process(_frames[frame]);
    }
    return NULL;
}

/* --- display side -------------------------------------------------------- */

typedef enum {
    VIEW_BARS,
    VIEW_METER,
    VIEW_HISTORY,
    VIEW_NUMOF,
} view_t;

static const char *const _view_names[] = { "SPECTRUM", "LEVEL", "HISTORY" };

static lv_obj_t *_views[VIEW_NUMOF];
static lv_obj_t *_bars[NUM_BANDS];
static lv_obj_t *_peaks[NUM_BANDS];
static lv_obj_t *_meter;
static lv_obj_t *_meter_label;
static lv_obj_t *_chart;
static lv_chart_series_t *_series;
static lv_obj_t *_title;
static lv_obj_t *_gain_label;
static lv_obj_t *_db_label;

static view_t _view;
static float _shown[NUM_BANDS];
static float _peak[NUM_BANDS];
static uint8_t _peak_hold[NUM_BANDS];
static float _shown_level = DB_MIN;
static buttons_t _buttons;

static int _db_to_pct(float db)
{
    float pct = (db - DB_MIN) * 100.0f / (DB_MAX - DB_MIN);
    if (pct < 0) {
        return 0;
    }
    if (pct > 100) {
        return 100;
    }
    return (int)pct;
}

static lv_color_t _level_color(int pct)
{
    if (pct > 85) {
        return lv_color_hex(0xff4040);
    }
    if (pct > 60) {
        return lv_color_hex(0xffc820);
    }
    return lv_color_hex(0x30e070);
}

static void _show_view(view_t view)
{
    for (unsigned i = 0; i < VIEW_NUMOF; i++) {
        lv_obj_set_hidden(_views[i], i != view);
    }
    lv_label_set_text(_title, _view_names[view]);
    _view = view;
}

static void _update_gain_label(void)
{
    lv_label_set_text_fmt(_gain_label, "GAIN +%d dB", _gains[_gain_idx]);
}

static void _refresh(lv_timer_t *timer)
{
    (void)timer;

    buttons_update(&_buttons);
    if (_buttons.pressed & BTN_A) {
        _show_view((_view + 1) % VIEW_NUMOF);
    }
    if (_buttons.pressed & BTN_B) {
        _gain_idx = (_gain_idx + 1) % ARRAY_SIZE(_gains);
        _update_gain_label();
        _mic_start();
    }

    /* fast attack, slower release */
    float level = _level_db;
    _shown_level = (level > _shown_level) ? level : _shown_level - 1.5f;
    if (_shown_level < DB_MIN) {
        _shown_level = DB_MIN;
    }
    lv_label_set_text_fmt(_db_label, "%d dB", (int)lroundf(_shown_level));

    switch (_view) {
    case VIEW_BARS: {
        const int32_t area_h = lv_obj_get_content_height(_views[VIEW_BARS]);
        for (unsigned b = 0; b < NUM_BANDS; b++) {
            float db = _band_db[b];
            _shown[b] = (db > _shown[b]) ? db : _shown[b] - 2.0f;
            int pct = _db_to_pct(_shown[b]);
            lv_bar_set_value(_bars[b], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(_bars[b], _level_color(pct), LV_PART_INDICATOR);

            if (_shown[b] >= _peak[b]) {
                _peak[b] = _shown[b];
                _peak_hold[b] = PEAK_HOLD;
            }
            else if (_peak_hold[b]) {
                _peak_hold[b]--;
            }
            else {
                _peak[b] -= 1.0f;
            }
            int peak_pct = _db_to_pct(_peak[b]);
            lv_obj_set_y(_peaks[b], area_h - 3 - (area_h - 3) * peak_pct / 100);
        }
        break;
    }
    case VIEW_METER: {
        int pct = _db_to_pct(_shown_level);
        lv_arc_set_value(_meter, pct);
        lv_obj_set_style_arc_color(_meter, _level_color(pct), LV_PART_INDICATOR);
        lv_label_set_text_fmt(_meter_label, "%d", (int)lroundf(_shown_level));
        break;
    }
    default:
        break;
    }

    /* the history runs in all views at about 10 points per second */
    static unsigned count;
    if (++count % 3 == 0) {
        lv_chart_set_next_value(_chart, _series, _db_to_pct(level));
    }

    /* keep lvgl awake */
    lvgl_wakeup();
}

static lv_obj_t *_make_view(lv_obj_t *parent)
{
    lv_obj_t *view = lv_obj_create(parent);
    lv_obj_set_size(view, lv_pct(100), 172);
    lv_obj_set_pos(view, 0, 30);
    lv_obj_set_style_bg_opa(view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view, 0, 0);
    lv_obj_set_style_pad_all(view, 4, 0);
    lv_obj_set_scrollable(view, false);
    return view;
}

static void _create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x080818), 0);
    lv_obj_set_scrollable(scr, false);

    _title = lv_label_create(scr);
    lv_obj_set_style_text_font(_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_title, lv_color_hex(0x9fb0ff), 0);
    lv_obj_align(_title, LV_ALIGN_TOP_LEFT, 8, 8);

    _gain_label = lv_label_create(scr);
    lv_obj_set_style_text_color(_gain_label, lv_color_hex(0xffc846), 0);
    lv_obj_align(_gain_label, LV_ALIGN_TOP_RIGHT, -8, 10);
    _update_gain_label();

    /* spectrum */
    _views[VIEW_BARS] = _make_view(scr);
    const int bar_w = 11;
    const int gap = 3;
    const int left = (240 - 8 - NUM_BANDS * (bar_w + gap) + gap) / 2;
    for (unsigned b = 0; b < NUM_BANDS; b++) {
        _bars[b] = lv_bar_create(_views[VIEW_BARS]);
        lv_obj_set_size(_bars[b], bar_w, lv_pct(100));
        lv_obj_set_pos(_bars[b], left + b * (bar_w + gap), 0);
        lv_bar_set_range(_bars[b], 0, 100);
        lv_obj_set_style_radius(_bars[b], 2, 0);
        lv_obj_set_style_radius(_bars[b], 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_bars[b], lv_color_hex(0x1a1a3a), 0);
        lv_obj_set_style_bg_opa(_bars[b], LV_OPA_COVER, 0);
        lv_obj_set_style_anim_duration(_bars[b], 0, 0);

        _peaks[b] = lv_obj_create(_views[VIEW_BARS]);
        lv_obj_set_size(_peaks[b], bar_w, 3);
        lv_obj_set_pos(_peaks[b], left + b * (bar_w + gap), 160);
        lv_obj_set_style_bg_color(_peaks[b], lv_color_white(), 0);
        lv_obj_set_style_border_width(_peaks[b], 0, 0);
        lv_obj_set_style_radius(_peaks[b], 0, 0);
        lv_obj_set_style_pad_all(_peaks[b], 0, 0);
        _shown[b] = DB_MIN;
        _peak[b] = DB_MIN;
    }

    /* level meter */
    _views[VIEW_METER] = _make_view(scr);
    _meter = lv_arc_create(_views[VIEW_METER]);
    lv_obj_set_size(_meter, 160, 160);
    lv_obj_center(_meter);
    lv_arc_set_bg_angles(_meter, 135, 45);
    lv_arc_set_range(_meter, 0, 100);
    lv_arc_set_value(_meter, 0);
    lv_obj_remove_style(_meter, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(_meter, false);
    lv_obj_set_style_arc_width(_meter, 16, 0);
    lv_obj_set_style_arc_width(_meter, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_meter, lv_color_hex(0x1a1a3a), 0);
    _meter_label = lv_label_create(_meter);
    lv_obj_set_style_text_font(_meter_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_meter_label, lv_color_white(), 0);
    lv_obj_center(_meter_label);
    lv_obj_t *unit = lv_label_create(_meter);
    lv_label_set_text(unit, "dBFS");
    lv_obj_set_style_text_color(unit, lv_color_hex(0x9fb0ff), 0);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);

    /* level history */
    _views[VIEW_HISTORY] = _make_view(scr);
    _chart = lv_chart_create(_views[VIEW_HISTORY]);
    lv_obj_set_size(_chart, lv_pct(100), lv_pct(100));
    lv_chart_set_type(_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_chart, HISTORY_POINTS);
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_chart, lv_color_hex(0x10102a), 0);
    lv_obj_set_style_border_color(_chart, lv_color_hex(0x30305a), 0);
    _series = lv_chart_add_series(_chart, lv_color_hex(0x30e0c0), LV_CHART_AXIS_PRIMARY_Y);
    for (unsigned i = 0; i < HISTORY_POINTS; i++) {
        lv_chart_set_next_value(_chart, _series, 0);
    }

    /* bottom line */
    _db_label = lv_label_create(scr);
    lv_obj_set_style_text_font(_db_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_db_label, lv_color_white(), 0);
    lv_obj_align(_db_label, LV_ALIGN_BOTTOM_LEFT, 8, -10);

    lv_obj_t *help = lv_label_create(scr);
    lv_label_set_text(help, "A: view   B: gain");
    lv_obj_set_style_text_color(help, lv_color_hex(0x7080b0), 0);
    lv_obj_align(help, LV_ALIGN_BOTTOM_RIGHT, -8, -12);

    _show_view(VIEW_BARS);
    lv_timer_create(_refresh, REFRESH_MS, NULL);
}

int main(void)
{
    buttons_init();
    _dsp_init();

    _dsp_pid = thread_create(_dsp_stack, sizeof(_dsp_stack), THREAD_PRIORITY_MAIN - 1,
                             0, _dsp_thread, NULL, "dsp");

    board_mic_prepare();
    _mic_start();

    disp_dev_backlight_on();
    _create_ui();
    lvgl_run();

    return 0;
}
