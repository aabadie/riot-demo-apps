/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Sound Spotter: sound event recognition with an emlearn random
 *              forest on the Adafruit CLUE
 *
 * The board listens with its PDM microphone, cuts sound events and
 * recognizes claps, finger snaps, whistles and knocks:
 * - a clap switches the lamp on or off
 * - a snap changes its color
 * - a whistle shows its pitch
 * - a knock asks who is there
 *
 * Record mode sends the events over the serial port, for tools/record.py,
 * to train a model on your own sounds with tools/train.py. The default
 * model is trained on synthetic sounds.
 *
 * Controls:
 * - A: switch the lamp off and reset the counters
 * - B: enter record mode, then select the next sound to record
 * - B (held for a second): back to recognition
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "irq.h"
#include "periph/pdm.h"
#include "thread.h"
#include "thread_flags.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "sound.h"

#define MIC_GAIN_DB     (10)
#define SKIP_FRAMES     (20U)       /* microphone start-up, about 300 ms */
#define RING_FRAMES     (SOUND_MAX_FRAMES + SOUND_STOP_FRAMES + 4)
#define FLAG_FRAME      (1U << 0)
#define FRAME_MS        (40U)
#define LONG_PRESS_MS   (1000U)
#define MIN_PROBA       (0.5f)
#define MUTE_MS         (150U)      /* microphone ignored after a button or a beep */
#define METER_MIN_DB    (-90.0f)
#define METER_MAX_DB    (26.0f)     /* a full scale sine */

#define COLOR_BG        GFX_RGB(8, 12, 20)
#define COLOR_PANEL     GFX_RGB(24, 32, 52)
#define COLOR_TEXT      GFX_RGB(225, 232, 255)
#define COLOR_DIM       GFX_RGB(110, 120, 150)
#define COLOR_ACCENT    GFX_RGB(80, 220, 200)
#define COLOR_REC       GFX_RGB(240, 60, 70)
#define COLOR_LEVEL     GFX_RGB(80, 160, 255)

static const char *const _names[SOUND_CLASSES] = SOUND_NAMES;
static const uint16_t _lamp_colors[] = {
    GFX_RGB(255, 210, 90), GFX_RGB(255, 90, 90), GFX_RGB(90, 255, 140),
    GFX_RGB(90, 160, 255), GFX_RGB(230, 110, 255),
};

/* audio, filled by the PDM interrupt */
static int16_t _pcm[RING_FRAMES][SOUND_FRAME];
static volatile unsigned _filled;   /* frames filled so far */
static unsigned _fill_pos;
static kernel_pid_t _dsp_pid = KERNEL_PID_UNDEF;
static char _dsp_stack[THREAD_STACKSIZE_DEFAULT + 2048];

/* analysis, by the DSP thread */
static sound_frame_t _frames[RING_FRAMES];
static sound_frame_t _event_frames[SOUND_MAX_FRAMES];
static int16_t _event_pcm[SOUND_MAX_FRAMES * SOUND_FRAME];
static volatile unsigned _event_pcm_frames;
static volatile bool _event_pcm_ready;
static volatile float _level = -100, _floor = -100;
static volatile bool _in_event;

/* results, written by the DSP thread with the interrupts off, copied by
   the main thread the same way */
typedef struct {
    int best;
    float proba;
    float pitch;
    unsigned onsets;
} result_t;

static volatile unsigned _result_seq;
static result_t _result;
/* the microphone is ignored until then */
static volatile uint32_t _mute_until;

static volatile bool _record;
static int _rec_label;
static unsigned _rec_count[SOUND_CLASSES];
static unsigned _count[SOUND_CLASSES];
static bool _lamp;
static unsigned _lamp_color;
static buttons_t _buttons;

static void _pdm_cb(void *arg, int16_t *buf)
{
    (void)arg;
    unsigned frame = _filled % RING_FRAMES;

    for (unsigned i = 0; i < PDM_BUF_SIZE; i++) {
        _pcm[frame][_fill_pos++] = buf[i];
        if (_fill_pos == SOUND_FRAME) {
            _fill_pos = 0;
            _filled++;
            frame = _filled % RING_FRAMES;
            thread_flags_set(thread_get(_dsp_pid), FLAG_FRAME);
        }
    }
}

static void _event_done(void)
{
    unsigned first;
    unsigned n = sound_event(&first);
    float pitch = 0;
    unsigned onsets = 0;

    for (unsigned i = 0; i < n; i++) {
        _event_frames[i] = _frames[(first + i) % RING_FRAMES];
        pitch += _event_frames[i].peak_hz;
        if (i > 0 && _event_frames[i].level > _event_frames[i - 1].level + 6.0f) {
            onsets++;
        }
    }

    if (_record) {
        if (!_event_pcm_ready) {
            for (unsigned i = 0; i < n; i++) {
                /* the samples ring counts the start-up frames too */
                unsigned slot = (first + SKIP_FRAMES + i) % RING_FRAMES;
                memcpy(&_event_pcm[i * SOUND_FRAME], _pcm[slot], sizeof(_pcm[0]));
            }
            _event_pcm_frames = n;
            _event_pcm_ready = true;
        }
        return;
    }

    int16_t features[SOUND_FEATURES];
    float proba[SOUND_CLASSES];
    if (sound_features(_event_frames, n, features) != 0) {
        return;
    }
    int best = sound_classify(features, proba);
    unsigned state = irq_disable();
    _result.best = best;
    _result.proba = best < 0 ? 0 : proba[best];
    _result.pitch = pitch / n;
    _result.onsets = onsets + 1;
    _result_seq++;
    irq_restore(state);
}

static void *_dsp_thread(void *arg)
{
    (void)arg;
    unsigned done = 0;

    while (1) {
        thread_flags_wait_any(FLAG_FRAME);
        while (done < _filled) {
            /* the detector counts frames from the first one it gets, the
               ring is indexed by the frame number since the start: skip the
               start-up frames on both sides */
            unsigned slot = done % RING_FRAMES;
            done++;
            if (done <= SKIP_FRAMES) {
                continue;
            }
            unsigned index = done - 1 - SKIP_FRAMES;
            sound_frame_t *f = &_frames[index % RING_FRAMES];
            sound_analyze(_pcm[slot], f);
            _level = f->level;
            if ((int32_t)(_mute_until - ztimer_now(ZTIMER_MSEC)) > 0) {
                sound_skip();
                _in_event = false;
                continue;
            }
            sound_event_t ev = sound_detect(f);
            _floor = sound_floor();
            _in_event = ev == SOUND_START ? true : (ev == SOUND_END ? false : _in_event);
            if (ev == SOUND_END) {
                _event_done();
            }
        }
    }
    return NULL;
}

/* display */

static void _header(void)
{
    int w = gfx_width();

    gfx_fill_rect(0, 0, w, 24, _record ? COLOR_REC : COLOR_PANEL);
    gfx_text_centered(4, _record ? "RECORD MODE" : "SOUND SPOTTER", COLOR_TEXT,
                      _record ? COLOR_REC : COLOR_PANEL, 2);
}

static int _db_x(float db, int w)
{
    float v = (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);

    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    return (int)(v * w);
}

static void _meter(void)
{
    int w = gfx_width() - 16;
    int y = 32;
    int x = _db_x(_level, w);
    int floor_x = _db_x(_floor, w);
    int start_x = _db_x(_floor + SOUND_START_DB, w);

    gfx_fill_rect(8, y, x, 10, _in_event ? COLOR_ACCENT : COLOR_LEVEL);
    gfx_fill_rect(8 + x, y, w - x, 10, COLOR_PANEL);
    gfx_fill_rect(8 + floor_x, y - 2, 2, 14, COLOR_DIM);
    gfx_fill_rect(8 + start_x, y - 2, 2, 14, COLOR_TEXT);
}

static void _lamp_draw(void)
{
    int cx = gfx_width() / 2;
    int cy = 110;
    int r = 38;
    uint16_t color = _lamp ? _lamp_colors[_lamp_color] : COLOR_PANEL;

    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) {
            dx++;
        }
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

static void _counters(void)
{
    char text[40];
    int h = gfx_height();

    snprintf(text, sizeof(text), "CLAP %u  SNAP %u", _count[0], _count[1]);
    gfx_fill_rect(0, h - 30, gfx_width(), 24, COLOR_BG);
    gfx_text_centered(h - 30, text, COLOR_DIM, COLOR_BG, 1);
    snprintf(text, sizeof(text), "WHISTLE %u  KNOCK %u", _count[2], _count[3]);
    gfx_text_centered(h - 18, text, COLOR_DIM, COLOR_BG, 1);
}

static void _message(const char *big, const char *small, uint16_t color)
{
    int w = gfx_width();

    gfx_fill_rect(0, 156, w, 46, COLOR_BG);
    gfx_text_centered(158, big, color, COLOR_BG, 3);
    gfx_text_centered(186, small, COLOR_DIM, COLOR_BG, 1);
}

static void _screen(void)
{
    char text[32];

    gfx_clear(COLOR_BG);
    _header();
    if (_record) {
        gfx_text_centered(80, _names[_rec_label], COLOR_ACCENT, COLOR_BG, 3);
        snprintf(text, sizeof(text), "RECORDED %u", _rec_count[_rec_label]);
        gfx_text_centered(112, text, COLOR_DIM, COLOR_BG, 1);
        gfx_text_centered(gfx_height() - 30, "MAKE THE SOUND", COLOR_TEXT,
                          COLOR_BG, 1);
        gfx_text_centered(gfx_height() - 18, "B: NEXT  HOLD B: BACK", COLOR_DIM,
                          COLOR_BG, 1);
    }
    else {
        _lamp_draw();
        _message("LISTENING", "CLAP SNAP WHISTLE KNOCK", COLOR_TEXT);
        _counters();
    }
}

static void _show_result(void)
{
    char text[32];
    unsigned state = irq_disable();
    result_t res = _result;
    irq_restore(state);
    int r = res.best;

    if (r < 0 || res.proba < MIN_PROBA) {
        snprintf(text, sizeof(text), "NOT SURE %d%%", (int)(res.proba * 100));
        _message("???", text, COLOR_DIM);
        printf("S UNKNOWN %d\n", (int)(res.proba * 100));
        return;
    }
    printf("S %s %d\n", _names[r], (int)(res.proba * 100 + 0.5f));
    _count[r]++;
    switch (r) {
    case 0:
        _lamp = !_lamp;
        _lamp_draw();
        snprintf(text, sizeof(text), "LAMP %s  %d%%", _lamp ? "ON" : "OFF",
                 (int)(res.proba * 100 + 0.5f));
        break;
    case 1:
        _lamp_color = (_lamp_color + 1) % ARRAY_SIZE(_lamp_colors);
        _lamp_draw();
        snprintf(text, sizeof(text), "NEW COLOR  %d%%",
                 (int)(res.proba * 100 + 0.5f));
        break;
    case 2:
        snprintf(text, sizeof(text), "PITCH %d HZ", (int)res.pitch);
        break;
    default:
        snprintf(text, sizeof(text), "%u KNOCKS, WHO IS THERE?", res.onsets);
        break;
    }
    _message(_names[r], text, _lamp_colors[r % ARRAY_SIZE(_lamp_colors)]);
    _counters();
}

static void _send_recording(void)
{
    static const char hex[] = "0123456789abcdef";
    unsigned n = _event_pcm_frames;
    char chunk[64 + 1];

    printf("R %s %u ", _names[_rec_label], n);
    for (unsigned i = 0; i < n * SOUND_FRAME; i += 16) {
        /* little endian 16 bit samples, 16 at a time */
        for (unsigned j = 0; j < 16; j++) {
            uint16_t v = (uint16_t)_event_pcm[i + j];
            chunk[4 * j] = hex[(v >> 4) & 0xf];
            chunk[4 * j + 1] = hex[v & 0xf];
            chunk[4 * j + 2] = hex[(v >> 12) & 0xf];
            chunk[4 * j + 3] = hex[(v >> 8) & 0xf];
        }
        chunk[64] = '\0';
        fputs(chunk, stdout);
    }
    puts("");
    _event_pcm_ready = false;
    _rec_count[_rec_label]++;
    _mute_until = ztimer_now(ZTIMER_MSEC) + 30 + MUTE_MS;
    tone_beep(NOTE_A5, 30);
    _screen();
}

int main(void)
{
    unsigned seen = 0;
    uint32_t b_down = 0;
    bool b_long = false;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    sound_init();

    _dsp_pid = thread_create(_dsp_stack, sizeof(_dsp_stack), THREAD_PRIORITY_MAIN - 1,
                             0, _dsp_thread, NULL, "dsp");

    board_mic_prepare();
    int32_t rate = pdm_init(PDM_MODE_MONO, SOUND_RATE, MIC_GAIN_DB, _pdm_cb, NULL);
    if (rate < 0) {
        printf("PDM init failed: %ld\n", (long)rate);
        return 1;
    }
    printf("Sound Spotter, PDM at %ld Hz\n", (long)rate);
    pdm_start();
    _screen();

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);
        buttons_update(&_buttons);
        if (_buttons.pressed || _buttons.released) {
            /* the click of a button is not a sound to record or spot */
            _mute_until = now + MUTE_MS;
        }

        if (_buttons.pressed & BTN_A && !_record) {
            _lamp = false;
            memset(_count, 0, sizeof(_count));
            _screen();
        }
        if (_buttons.pressed & BTN_B) {
            b_down = now;
            b_long = false;
        }
        if ((_buttons.held & BTN_B) && !b_long && _record &&
            now - b_down >= LONG_PRESS_MS) {
            b_long = true;
            _record = false;
            _screen();
        }
        if ((_buttons.released & BTN_B) && !b_long) {
            if (_record) {
                _rec_label = (_rec_label + 1) % SOUND_CLASSES;
            }
            _record = true;
            _screen();
        }

        if (_record && _event_pcm_ready) {
            _send_recording();
        }
        if (_result_seq != seen) {
            seen = _result_seq;
            if (!_record) {
                _show_result();
            }
        }
        _meter();
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
