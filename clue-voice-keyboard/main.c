/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Voice Keyboard: the Adafruit CLUE types what you spell
 *
 * Teach the CLUE your voice saying the NATO alphabet, the digits and four
 * commands (SPACE, ENTER, DELETE, SHIFT), then spell: the board is a USB
 * keyboard and types the letters on the computer. See voice.c for the
 * recognition. The examples are saved in flash and survive a reset.
 *
 * Controls, keyboard mode:
 * - A: typing on or off (the words are still recognized and shown)
 * - B: teach mode
 *
 * Controls, teach mode (say the word on the screen 3 times, it then moves
 * on to the next one):
 * - A: next word, held for a second: previous word
 * - B: back to the keyboard mode
 *
 * Holding A and B for 3 seconds forgets all the words.
 */

#include <stdio.h>
#include <string.h>

#include "auto_init_utils.h"
#include "auto_init_priorities.h"
#include "msg.h"
#include "mutex.h"
#include "periph/flashpage.h"
#include "periph/pdm.h"
#include "thread.h"
#include "thread_flags.h"
#include "usb/hid.h"
#include "usb/usbus.h"
#include "usb/usbus/hid.h"
#include "usb/usbus/hid_io.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "keys.h"
#include "voice.h"

#define MIC_GAIN_DB     (10)
#define SKIP_FRAMES     (20U)       /* microphone start-up, about 300 ms */
#define FRAME_MS        (40U)
#define RESULT_SHOW_MS  (1500U)
#define LONG_PRESS_MS   (1000U)
#define FORGET_MS       (3000U)
#define FLAG_FRAME      (1U << 0)
#define NUM_FRAMES      (8U)        /* 128 ms of audio, a classification takes ~100 ms */
#define ECHO_MS         (80U)       /* after a sound of the speaker */
#define TEXT_LEN        (64U)
#define TEXT_SHOWN      (19U)

#define GRID_COLS       (10)
#define GRID_X          (5)
#define GRID_Y          (102)
#define CELL            (22)

#define COLOR_BG        GFX_RGB(10, 10, 24)
#define COLOR_PANEL     GFX_RGB(30, 30, 56)
#define COLOR_TEXT      GFX_RGB(230, 232, 255)
#define COLOR_DIM       GFX_RGB(120, 124, 160)
#define COLOR_LEVEL     GFX_RGB(60, 220, 120)
#define COLOR_SPEECH    GFX_RGB(255, 60, 60)
#define COLOR_ON        GFX_RGB(60, 220, 120)
#define COLOR_OFF       GFX_RGB(200, 80, 80)
#define COLOR_HIT       GFX_RGB(255, 210, 60)
#define COLOR_TEACH     GFX_RGB(255, 255, 255)

static_assert(KEYS_NUM_WORDS == VOICE_NUM_WORDS, "vocabulary size mismatch");

/* --- USB: serial console and keyboard ------------------------------------ */

/* a boot keyboard: modifiers, a reserved byte and 6 keys */
static const uint8_t _report_desc[] = {
    USB_HID_USAGE_PAGE(0x01),                   /* generic desktop */
    USB_HID_USAGE(0x06),                        /* keyboard */
    USB_HID_COLLECTION(USB_HID_COLLECTION_APPLICATION),
        USB_HID_USAGE_PAGE(0x07),               /* keys */
        USB_HID_USAGE_MIN8(0xe0),
        USB_HID_USAGE_MAX8(0xe7),
        USB_HID_LOGICAL_MIN8(0),
        USB_HID_LOGICAL_MAX8(1),
        USB_HID_REPORT_SIZE(1),
        USB_HID_REPORT_COUNT(8),
        USB_HID_INPUT(0x02),                    /* modifiers */
        USB_HID_REPORT_SIZE(8),
        USB_HID_REPORT_COUNT(1),
        USB_HID_INPUT(0x01),                    /* reserved */
        USB_HID_USAGE_PAGE(0x08),               /* LEDs */
        USB_HID_USAGE_MIN8(1),
        USB_HID_USAGE_MAX8(5),
        USB_HID_REPORT_SIZE(1),
        USB_HID_REPORT_COUNT(5),
        USB_HID_OUTPUT(0x02),
        USB_HID_REPORT_SIZE(3),
        USB_HID_REPORT_COUNT(1),
        USB_HID_OUTPUT(0x01),                   /* padding */
        USB_HID_USAGE_PAGE(0x07),               /* keys */
        USB_HID_USAGE_MIN8(0),
        USB_HID_USAGE_MAX8(0x65),
        USB_HID_LOGICAL_MIN8(0),
        USB_HID_LOGICAL_MAX8(0x65),
        USB_HID_REPORT_SIZE(8),
        USB_HID_REPORT_COUNT(6),
        USB_HID_INPUT(0x00),                    /* keys */
    USB_HID_END_COLLECTION,
};

static usbus_t _usbus;
static char _usbus_stack[USBUS_STACKSIZE];

/* replaces auto_init_usbus: the same USB device, with the keyboard next to
   the serial console */
static void _usb_init(void)
{
    void usb_cdc_acm_stdio_init(usbus_t *usbus);

    usbus_init(&_usbus, usbdev_get_ctx(0));
    usb_cdc_acm_stdio_init(&_usbus);
    usb_hid_io_init(&_usbus, _report_desc, sizeof(_report_desc));
    usbus_create(_usbus_stack, USBUS_STACKSIZE, USBUS_PRIO, USBUS_TNAME, &_usbus);
}

AUTO_INIT(_usb_init, AUTO_INIT_PRIO_MOD_USBUS);

static bool _usb_ready(void)
{
    return _usbus.state == USBUS_STATE_CONFIGURED;
}

/* the keys are typed by their own thread, a write waits for the computer */
static char _typer_stack[THREAD_STACKSIZE_SMALL];
static kernel_pid_t _typer_pid = KERNEL_PID_UNDEF;

static void *_typer_thread(void *arg)
{
    (void)arg;
    static msg_t queue[16];
    msg_init_queue(queue, ARRAY_SIZE(queue));

    while (1) {
        msg_t msg;
        msg_receive(&msg);
        uint8_t report[8] = { 0 };
        if (!_usb_ready()) {
            continue;
        }
        report[0] = msg.content.value >> 8;
        report[2] = msg.content.value & 0xff;
        usb_hid_io_write(report, sizeof(report));
        memset(report, 0, sizeof(report));
        usb_hid_io_write(report, sizeof(report));
    }
    return NULL;
}

static bool _type(char ch)
{
    uint8_t mod, usage;
    msg_t msg;

    if (!keys_lookup(ch, &mod, &usage) || !_usb_ready()) {
        return false;
    }
    msg.content.value = ((uint32_t)mod << 8) | usage;
    return msg_try_send(&msg, _typer_pid) == 1;
}

/* --- the examples in flash ----------------------------------------------- */

#define STORE_PAGES     ((sizeof(voice_store_t) + FLASHPAGE_SIZE - 1) / FLASHPAGE_SIZE)

FLASH_WRITABLE_INIT(_flash_store, STORE_PAGES);
static uint32_t _page[FLASHPAGE_SIZE / sizeof(uint32_t)];
static mutex_t _voice_lock = MUTEX_INIT;

/* write the pages that changed, about 85 ms each. The first page, with the
   magic and the number of examples, is written last: after a reset in the
   middle, the old header comes with at most one new example, in the slot
   of the old one it replaced */
static void _save(void)
{
    unsigned first = flashpage_page(_flash_store);
    size_t len = sizeof(voice_store_t);
    unsigned written = 0;

    for (unsigned i = 1; i <= STORE_PAGES; i++) {
        size_t off = (i % STORE_PAGES) * FLASHPAGE_SIZE;
        size_t n = len - off < FLASHPAGE_SIZE ? len - off : FLASHPAGE_SIZE;
        memset(_page, 0xff, sizeof(_page));
        mutex_lock(&_voice_lock);
        memcpy(_page, (const uint8_t *)voice_store() + off, n);
        mutex_unlock(&_voice_lock);
        if (memcmp(_flash_store + off, _page, n) == 0) {
            continue;
        }
        if (flashpage_write_and_verify(first + off / FLASHPAGE_SIZE, _page) != FLASHPAGE_OK) {
            puts("flash write failed");
            return;
        }
        written++;
    }
    printf("saved, %u pages written\n", written);
}

/* --- audio side ---------------------------------------------------------- */

/* a ring of frames: the callback fills frame _done % NUM_FRAMES, the voice
   thread reads the ones before */
static int16_t _frames[NUM_FRAMES][VOICE_FRAME_LEN];
static volatile unsigned _done;
static volatile unsigned _fill_pos;
/* the microphone is ignored until then, the speaker is playing */
static volatile uint32_t _mute_until;
static kernel_pid_t _dsp_pid = KERNEL_PID_UNDEF;
static volatile unsigned _result_seq;
static volatile unsigned _taught_seq;
static char _dsp_stack[THREAD_STACKSIZE_DEFAULT + 1024];

static void _pdm_cb(void *arg, int16_t *buf)
{
    (void)arg;
    unsigned pos = _fill_pos;

    for (unsigned i = 0; i < PDM_BUF_SIZE; i++) {
        _frames[_done % NUM_FRAMES][pos++] = buf[i];
        if (pos == VOICE_FRAME_LEN) {
            _done++;
            pos = 0;
            thread_flags_set(thread_get(_dsp_pid), FLAG_FRAME);
        }
    }
    _fill_pos = pos;
}

static void *_dsp_thread(void *arg)
{
    (void)arg;
    unsigned skip = SKIP_FRAMES;
    unsigned read = 0;
    bool muted = false;

    while (1) {
        thread_flags_wait_any(FLAG_FRAME);
        while (read != _done) {
            if (_done - read > NUM_FRAMES - 1) {
                /* too late, the oldest frames are being overwritten */
                read = _done - (NUM_FRAMES - 1);
            }
            const int16_t *frame = _frames[read % NUM_FRAMES];
            read++;
            if (skip) {
                skip--;
                continue;
            }
            if ((int32_t)(_mute_until - ztimer_now(ZTIMER_MSEC)) > 0) {
                muted = true;
                continue;
            }
            mutex_lock(&_voice_lock);
            if (muted) {
                muted = false;
                voice_restart();
            }
            voice_event_t event = voice_process(frame);
            mutex_unlock(&_voice_lock);
            if (event == VOICE_EVENT_TAUGHT) {
                _taught_seq++;
            }
            else if (event == VOICE_EVENT_RESULT) {
                _result_seq++;
            }
        }
    }
    return NULL;
}

/* --- display and controls ------------------------------------------------ */

static buttons_t _buttons;
static bool _teach_mode;
static bool _typing = true;
static bool _shift;
static int _word;                   /* word taught */
static unsigned _taught_here;       /* examples of it since it was shown */
static int _hit = -1;               /* word recognized, highlighted */
static uint32_t _hit_time;
static char _text[TEXT_LEN + 1];
static unsigned _text_len;

static void _draw_cell(unsigned w)
{
    int x = GRID_X + (int)(w % GRID_COLS) * (CELL + 1);
    int y = GRID_Y + (int)(w / GRID_COLS) * (CELL + 2);
    static const uint16_t fill[VOICE_NUM_EXAMPLES + 1] = {
        GFX_RGB(40, 40, 60), GFX_RGB(120, 80, 30), GFX_RGB(180, 130, 30),
        GFX_RGB(40, 150, 80),
    };
    uint16_t frame = fill[voice_num_examples(w)];
    const char *label = keys_words[w].label;

    if (_teach_mode && (int)w == _word) {
        frame = COLOR_TEACH;
    }
    else if ((int)w == _hit) {
        frame = COLOR_HIT;
    }
    gfx_fill_rect(x, y, CELL, CELL, frame);
    gfx_fill_rect(x + 2, y + 2, CELL - 4, CELL - 4, fill[voice_num_examples(w)]);
    gfx_text(x + (CELL - gfx_text_width(label, 1)) / 2, y + (CELL - GFX_CHAR_H) / 2,
             label, COLOR_TEXT, fill[voice_num_examples(w)], 1);
}

static void _draw_grid(void)
{
    for (unsigned w = 0; w < VOICE_NUM_WORDS; w++) {
        _draw_cell(w);
    }
}

static void _big(const char *word, const char *detail, uint16_t color)
{
    /* down to the status line, at y = 74 */
    gfx_fill_rect(0, 26, gfx_width(), 48, COLOR_BG);
    gfx_text_centered(30, word, color, COLOR_BG, 3);
    if (detail) {
        gfx_text_centered(60, detail, COLOR_DIM, COLOR_BG, 1);
    }
}

static void _status(void)
{
    char line[40];

    gfx_fill_rect(0, 74, gfx_width(), 10, COLOR_BG);
    if (_teach_mode) {
        snprintf(line, sizeof(line), "TEACH  WORD %d/%u  %u WORDS KNOWN", _word + 1,
                 VOICE_NUM_WORDS, voice_num_words());
        gfx_text_centered(74, line, COLOR_TEACH, COLOR_BG, 1);
    }
    else {
        snprintf(line, sizeof(line), "TYPING %s  USB %s%s", _typing ? "ON" : "OFF",
                 _usb_ready() ? "OK" : "--", _shift ? "  SHIFT" : "");
        gfx_text_centered(74, line, _typing && _usb_ready() ? COLOR_ON : COLOR_OFF,
                          COLOR_BG, 1);
    }
}

static void _draw_text(void)
{
    char shown[TEXT_SHOWN + 2];
    unsigned start = _text_len > TEXT_SHOWN ? _text_len - TEXT_SHOWN : 0;

    snprintf(shown, sizeof(shown), "%s", _text + start);
    gfx_fill_rect(0, 202, gfx_width(), 20, COLOR_PANEL);
    gfx_text(4, 205, shown, COLOR_TEXT, COLOR_PANEL, 2);
    /* the cursor, the font has no underscore */
    gfx_fill_rect(4 + gfx_text_width(shown, 2), 219, 10, 2, COLOR_TEXT);
}

static void _help(void)
{
    gfx_fill_rect(0, 228, gfx_width(), 12, COLOR_BG);
    gfx_text_centered(229, _teach_mode ? "A: NEXT (HOLD: BACK)  B: DONE"
                                       : "A: TYPING ON/OFF  B: TEACH",
                      COLOR_DIM, COLOR_BG, 1);
}

static void _show_word_to_teach(void)
{
    char detail[32];

    snprintf(detail, sizeof(detail), "SAY IT 3 TIMES  %u/3", _taught_here);
    _big(keys_words[_word].name, detail, COLOR_TEACH);
    _status();
}

static void _teach(int word)
{
    int old = _word;

    _word = (word + VOICE_NUM_WORDS) % VOICE_NUM_WORDS;
    _taught_here = 0;
    voice_teach(_word);
    _draw_cell(old);
    _draw_cell(_word);
    _show_word_to_teach();
}

static void _set_mode(bool teach)
{
    _teach_mode = teach;
    if (teach) {
        /* start at the first word that needs examples */
        _word = 0;
        while (_word < (int)VOICE_NUM_WORDS - 1 &&
               voice_num_examples(_word) >= VOICE_NUM_EXAMPLES) {
            _word++;
        }
        _teach(_word);
    }
    else {
        voice_teach(-1);
        _draw_cell(_word);
        _big("...", voice_num_words() ? "SPELL A WORD" : "TEACH THE WORDS FIRST, PRESS B",
             COLOR_DIM);
        _status();
    }
    _help();
}

static void _text_add(char ch)
{
    if (ch == KEYS_CHAR_DELETE) {
        if (_text_len) {
            _text[--_text_len] = '\0';
        }
    }
    else if (ch == KEYS_CHAR_ENTER) {
        _text_len = 0;
        _text[0] = '\0';
    }
    else {
        if (_text_len == TEXT_LEN) {
            memmove(_text, _text + 1, TEXT_LEN - 1);
            _text_len--;
        }
        _text[_text_len++] = ch;
        _text[_text_len] = '\0';
    }
    _draw_text();
}

/* ignore the microphone for the next ms, and the echo after it */
static void _mute_mic(unsigned ms)
{
    _mute_until = ztimer_now(ZTIMER_MSEC) + ms + ECHO_MS;
}

static void _on_taught(void)
{
    static const tone_note_t ok[] = { { NOTE_E6, 40 }, { NOTE_REST, 30 }, { NOTE_E6, 40 } };

    /* the CPU stalls while the flash is written, then the speaker plays: none
       of it is the next example */
    _mute_mic(1000);
    _save();
    _mute_mic(110);
    tone_effect(ok, ARRAY_SIZE(ok));
    if (!_teach_mode) {
        return;
    }
    _taught_here++;
    _draw_cell(_word);
    if (_taught_here < VOICE_NUM_EXAMPLES) {
        voice_teach(_word);
        _show_word_to_teach();
    }
    else if (_word + 1 < (int)VOICE_NUM_WORDS) {
        _teach(_word + 1);
    }
    else {
        _set_mode(false);
        _big("DONE", "ALL THE WORDS ARE TAUGHT", COLOR_ON);
    }
}

static void _on_result(uint32_t now)
{
    int w = voice_result_word();
    char detail[40];
    float dist = voice_result_dist();

    if (_teach_mode) {
        return;
    }
    if (w < 0) {
        int best = voice_result_best();
        snprintf(detail, sizeof(detail), "NOT SURE, %s?", best >= 0 ? keys_words[best].name : "");
        _big("?", detail, COLOR_DIM);
        _mute_mic(40);
        tone_beep(NOTE_C4, 40);
        return;
    }

    char ch = keys_words[w].ch;
    bool typed = false;
    if (ch == KEYS_CHAR_SHIFT) {
        _shift = !_shift;
    }
    else {
        if (_shift && ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        _shift = false;
        _text_add(ch);
        if (_typing) {
            typed = _type(ch);
        }
    }
    snprintf(detail, sizeof(detail), "%s  DISTANCE %d.%02d", typed ? "TYPED" : "SEEN",
             (int)dist, (int)(dist * 100) % 100);
    _big(keys_words[w].name, detail, COLOR_HIT);
    printf("K %s %c\n", keys_words[w].name, ch >= ' ' ? ch : '.');
    _mute_mic(30);
    tone_beep(NOTE_A5, 30);

    int old = _hit;
    _hit = w;
    _hit_time = now;
    if (old >= 0) {
        _draw_cell(old);
    }
    _draw_cell(w);
    _status();
}

static void _draw_static(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(0, 0, gfx_width(), 22, COLOR_PANEL);
    gfx_text(6, 4, "VOICE KEYBOARD", COLOR_TEXT, COLOR_PANEL, 2);
    _draw_grid();
    _draw_text();
}

static void _level(void)
{
    static int drawn = -1;
    static bool drawn_speech;
    int w = (int)(voice_level_db() * 4);
    bool speech = voice_in_speech();
    int max = gfx_width() - 8;

    w = w < 0 ? 0 : (w > max ? max : w);
    if (w == drawn && speech == drawn_speech) {
        return;
    }
    gfx_fill_rect(4, 90, w, 6, speech ? COLOR_SPEECH : COLOR_LEVEL);
    gfx_fill_rect(4 + w, 90, max - w, 6, COLOR_PANEL);
    gfx_fill_rect(gfx_width() - 18, 6, 10, 10, speech ? COLOR_SPEECH : COLOR_PANEL);
    drawn = w;
    drawn_speech = speech;
}

int main(void)
{
    unsigned seen_result = 0, seen_taught = 0;
    uint32_t a_down = 0, both_down = 0;
    bool a_long = false, combo = false, usb_was_ready = false;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    if (voice_init() != 0) {
        puts("voice_init failed");
        return 1;
    }
    /* the flash pages are page aligned */
    const void *saved = _flash_store;
    if (voice_load(saved) == 0) {
        printf("%u words loaded from flash\n", voice_num_words());
    }

    _typer_pid = thread_create(_typer_stack, sizeof(_typer_stack), THREAD_PRIORITY_MAIN - 1,
                               0, _typer_thread, NULL, "typer");
    _dsp_pid = thread_create(_dsp_stack, sizeof(_dsp_stack), THREAD_PRIORITY_MAIN - 2,
                             0, _dsp_thread, NULL, "voice");

    board_mic_prepare();
    int32_t rate = pdm_init(PDM_MODE_MONO, VOICE_SAMPLE_RATE, MIC_GAIN_DB, _pdm_cb, NULL);
    if (rate < 0) {
        printf("PDM init failed: %ld\n", (long)rate);
        return 1;
    }
    pdm_start();
    puts("Voice Keyboard");

    _draw_static();
    _set_mode(voice_num_words() == 0);

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);
        buttons_update(&_buttons);

        /* A and B held together: forget everything. The buttons do nothing
           else until both are released. */
        if ((_buttons.held & (BTN_A | BTN_B)) == (BTN_A | BTN_B)) {
            if (!combo) {
                combo = true;
                both_down = now;
            }
            else if (both_down && now - both_down >= FORGET_MS) {
                both_down = 0;
                mutex_lock(&_voice_lock);
                voice_forget_all();
                mutex_unlock(&_voice_lock);
                _mute_mic(1000);
                _save();
                _mute_mic(300);
                tone_beep(NOTE_C4, 300);
                _draw_grid();
                _set_mode(true);
            }
        }
        else if (combo && !(_buttons.held & (BTN_A | BTN_B))) {
            combo = false;
        }
        else if (!combo) {
            if (_buttons.pressed & BTN_A) {
                a_down = now;
                a_long = false;
            }
            if (_teach_mode && (_buttons.held & BTN_A) && !a_long &&
                now - a_down >= LONG_PRESS_MS) {
                a_long = true;
                _teach(_word - 1);
            }
            if ((_buttons.released & BTN_A) && !a_long) {
                if (_teach_mode) {
                    _teach(_word + 1);
                }
                else {
                    _typing = !_typing;
                    _status();
                }
            }
            if (_buttons.released & BTN_B) {
                _set_mode(!_teach_mode);
            }
        }

        if (_taught_seq != seen_taught) {
            seen_taught = _taught_seq;
            _on_taught();
        }
        if (_result_seq != seen_result) {
            seen_result = _result_seq;
            _on_result(now);
        }
        if (_hit >= 0 && now - _hit_time > RESULT_SHOW_MS) {
            int old = _hit;
            _hit = -1;
            _draw_cell(old);
        }
        if (_usb_ready() != usb_was_ready) {
            usb_was_ready = _usb_ready();
            _status();
        }
        _level();
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
