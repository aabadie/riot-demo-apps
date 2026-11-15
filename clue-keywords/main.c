/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Color Words: keyword spotting taught on the board
 *
 * Teach the CLUE four words, then say them: the screen takes the color of
 * the word. See kws.c for the processing. Button A selects a word, button B
 * records the next utterance as an example of the selected word (up to 3 per
 * word, the oldest is replaced); B again cancels.
 */

#include <stdio.h>

#include "periph/pdm.h"
#include "thread.h"
#include "thread_flags.h"
#include "ztimer.h"

#include "lvgl/lvgl.h"
#include "lvgl_riot.h"

#include "demo_board.h"
#include "demo_tone.h"
#include "kws.h"

#define MIC_GAIN_DB     (10)
#define SKIP_FRAMES     (20U)       /* microphone start-up, about 300 ms */
#define REFRESH_MS      (40U)
#define RESULT_SHOW_MS  (1500U)
#define FLAG_FRAME      (1U << 0)
#define ECHO_MS         (80U)       /* after a sound of the speaker */

#define COLOR_IDLE      (0x080818)

typedef struct {
    const char *name;
    uint32_t color;
    uint16_t tone;
} word_t;

static const word_t _words[KWS_NUM_WORDS] = {
    { "RED", 0xe03040, NOTE_C5 },
    { "GREEN", 0x30c060, NOTE_E5 },
    { "BLUE", 0x3070ff, NOTE_G5 },
    { "YELLOW", 0xe0b020, NOTE_C6 },
};

/* --- audio side ---------------------------------------------------------- */

static int16_t _frames[2][KWS_FRAME_LEN];
/* the microphone is ignored until then, the speaker is playing */
static volatile uint32_t _mute_until;
static volatile unsigned _fill_frame;
static volatile unsigned _fill_pos;
static volatile int _ready_frame = -1;
static kernel_pid_t _dsp_pid = KERNEL_PID_UNDEF;

/* events for the display */
static volatile unsigned _result_seq;
static volatile unsigned _taught_seq;

static char _dsp_stack[THREAD_STACKSIZE_DEFAULT + 1024];

static void _pdm_cb(void *arg, int16_t *buf)
{
    (void)arg;
    unsigned pos = _fill_pos;
    unsigned frame = _fill_frame;

    for (unsigned i = 0; i < PDM_BUF_SIZE; i++) {
        _frames[frame][pos++] = buf[i];
        if (pos == KWS_FRAME_LEN) {
            _ready_frame = (int)frame;
            frame ^= 1;
            pos = 0;
            thread_flags_set(thread_get(_dsp_pid), FLAG_FRAME);
        }
    }
    _fill_pos = pos;
    _fill_frame = frame;
}

static void *_dsp_thread(void *arg)
{
    (void)arg;
    unsigned skip = SKIP_FRAMES;
    bool muted = false;

    while (1) {
        thread_flags_wait_any(FLAG_FRAME);
        int frame = _ready_frame;
        if (frame < 0) {
            continue;
        }
        if (skip) {
            skip--;
            continue;
        }
        if ((int32_t)(_mute_until - ztimer_now(ZTIMER_MSEC)) > 0) {
            muted = true;
            continue;
        }
        if (muted) {
            muted = false;
            kws_restart();
        }
        switch (kws_process(_frames[frame])) {
        case KWS_EVENT_TAUGHT:
            _taught_seq++;
            break;
        case KWS_EVENT_RESULT:
            _result_seq++;
            break;
        default:
            break;
        }
    }
    return NULL;
}

/* --- display side -------------------------------------------------------- */

static lv_obj_t *_result_label;
static lv_obj_t *_dist_label;
static lv_obj_t *_status_label;
static lv_obj_t *_level_bar;
static lv_obj_t *_speech_dot;
static lv_obj_t *_cards[KWS_NUM_WORDS];
static lv_obj_t *_card_counts[KWS_NUM_WORDS];

static buttons_t _buttons;
static unsigned _selected;
static unsigned _seen_result;
static unsigned _seen_taught;
static uint32_t _result_time;

static void _update_cards(void)
{
    static const char *const dots[] = { "- - -", "o - -", "o o -", "o o o" };

    for (unsigned w = 0; w < KWS_NUM_WORDS; w++) {
        bool sel = (w == _selected);
        lv_obj_set_style_border_width(_cards[w], sel ? 3 : 0, 0);
        lv_obj_set_style_bg_opa(_cards[w], sel ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_label_set_text(_card_counts[w], dots[kws_num_examples(w)]);
    }
}

static void _set_status(void)
{
    int teach = kws_teaching();
    if (teach >= 0) {
        lv_label_set_text_fmt(_status_label, "Say \"%s\" now...", _words[teach].name);
    }
    else {
        lv_label_set_text(_status_label, "A: select   B: teach");
    }
}

/* ignore the microphone while the speaker plays, and the echo after it */
static void _mute_mic(unsigned ms)
{
    _mute_until = ztimer_now(ZTIMER_MSEC) + ms + ECHO_MS;
}

static void _show_result(void)
{
    lv_obj_t *scr = lv_screen_active();
    int w = kws_result_word();
    float dist = kws_result_dist();

    if (w >= 0) {
        lv_label_set_text(_result_label, _words[w].name);
        lv_obj_set_style_bg_color(scr, lv_color_hex(_words[w].color), 0);
        _mute_mic(120);
        tone_beep(_words[w].tone, 120);
    }
    else {
        lv_label_set_text(_result_label, "?");
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x202030), 0);
    }
    lv_label_set_text_fmt(_dist_label, "distance %d.%02d", (int)dist, (int)(dist * 100) % 100);
}

static void _refresh(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = ztimer_now(ZTIMER_MSEC);

    buttons_update(&_buttons);
    if ((_buttons.pressed & BTN_A) && kws_teaching() < 0) {
        _selected = (_selected + 1) % KWS_NUM_WORDS;
        _update_cards();
    }
    if (_buttons.pressed & BTN_B) {
        kws_teach(kws_teaching() < 0 ? (int)_selected : -1);
        _mute_mic(30);
        tone_beep(NOTE_A5, 30);
        _set_status();
    }

    int level = (int)(kws_level_db() * 4);
    lv_bar_set_value(_level_bar, level < 0 ? 0 : (level > 100 ? 100 : level), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_speech_dot, kws_in_speech() ? lv_color_hex(0xff3030)
                                                           : lv_color_hex(0x303050), 0);

    if (_taught_seq != _seen_taught) {
        static const tone_note_t ok[] = { { NOTE_E6, 60 }, { NOTE_REST, 40 }, { NOTE_E6, 60 } };
        _seen_taught = _taught_seq;
        _mute_mic(160);
        tone_effect(ok, ARRAY_SIZE(ok));
        _update_cards();
        _set_status();
    }

    if (_result_seq != _seen_result) {
        _seen_result = _result_seq;
        _result_time = now;
        _show_result();
    }

    if (_result_time && now - _result_time > RESULT_SHOW_MS) {
        _result_time = 0;
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(COLOR_IDLE), 0);
        lv_label_set_text(_result_label, "...");
    }

    lvgl_wakeup();
}

static void _create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_IDLE), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "COLOR WORDS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xc0c8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    _speech_dot = lv_obj_create(scr);
    lv_obj_set_size(_speech_dot, 12, 12);
    lv_obj_set_style_radius(_speech_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_speech_dot, 0, 0);
    lv_obj_align(_speech_dot, LV_ALIGN_TOP_RIGHT, -8, 10);

    _level_bar = lv_bar_create(scr);
    lv_obj_set_size(_level_bar, 90, 8);
    lv_obj_align(_level_bar, LV_ALIGN_TOP_RIGHT, -28, 12);
    lv_bar_set_range(_level_bar, 0, 100);
    lv_obj_set_style_bg_color(_level_bar, lv_color_hex(0x30e070), LV_PART_INDICATOR);

    _result_label = lv_label_create(scr);
    lv_label_set_text(_result_label, "...");
    lv_obj_set_style_text_font(_result_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_result_label, lv_color_white(), 0);
    lv_obj_align(_result_label, LV_ALIGN_CENTER, 0, -45);

    _dist_label = lv_label_create(scr);
    lv_label_set_text(_dist_label, "teach the words first");
    lv_obj_set_style_text_color(_dist_label, lv_color_hex(0xc0c8ff), 0);
    lv_obj_align(_dist_label, LV_ALIGN_CENTER, 0, -12);

    for (unsigned w = 0; w < KWS_NUM_WORDS; w++) {
        _cards[w] = lv_obj_create(scr);
        lv_obj_set_size(_cards[w], 54, 52);
        lv_obj_set_pos(_cards[w], 6 + w * 58, 140);
        lv_obj_set_scrollable(_cards[w], false);
        lv_obj_set_style_pad_all(_cards[w], 2, 0);
        lv_obj_set_style_radius(_cards[w], 6, 0);
        lv_obj_set_style_bg_color(_cards[w], lv_color_hex(_words[w].color), 0);
        lv_obj_set_style_border_color(_cards[w], lv_color_white(), 0);

        lv_obj_t *name = lv_label_create(_cards[w]);
        lv_label_set_text(name, _words[w].name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 4);

        _card_counts[w] = lv_label_create(_cards[w]);
        lv_obj_set_style_text_color(_card_counts[w], lv_color_white(), 0);
        lv_obj_align(_card_counts[w], LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    _status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_status_label, lv_color_white(), 0);
    lv_obj_align(_status_label, LV_ALIGN_BOTTOM_MID, 0, -14);

    _update_cards();
    _set_status();
    lv_timer_create(_refresh, REFRESH_MS, NULL);
}

int main(void)
{
    buttons_init();
    tone_init(board_speaker_init());
    if (kws_init() != 0) {
        puts("kws_init failed");
        return 1;
    }

    _dsp_pid = thread_create(_dsp_stack, sizeof(_dsp_stack), THREAD_PRIORITY_MAIN - 1,
                             0, _dsp_thread, NULL, "kws");

    board_mic_prepare();
    int32_t rate = pdm_init(PDM_MODE_MONO, KWS_SAMPLE_RATE, MIC_GAIN_DB, _pdm_cb, NULL);
    if (rate < 0) {
        printf("PDM init failed: %ld\n", (long)rate);
        return 1;
    }
    printf("PDM running at %ld Hz\n", (long)rate);
    pdm_start();

    disp_dev_backlight_on();
    _create_ui();
    lvgl_run();

    return 0;
}
