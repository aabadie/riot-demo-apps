/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Digit Oracle: handwritten digit recognition with emlearn
 *
 * Digits come from two sources:
 * - tools/send_digits.py sends MNIST test digits over the USB serial port,
 *   the board shows the digit, its answer and the running score
 * - digits drawn on the board, with the d-pad on the PyBadge or by tilting
 *   the CLUE, are recognized while they are drawn, and sent to
 *   send_digits.py listen once the pen has been up for a while
 *
 * Controls: hold A to draw, B clears. On the PyBadge, the d-pad moves the
 * pen and SELECT toggles the sound.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "kernel_defines.h"
#include "mutex.h"
#include "thread.h"
#include "ztimer.h"

#include "demo_accel.h"
#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "digits.h"
#include "protocol.h"

#define FRAME_MS        (20U)
#define CLASSIFY_MS     (200U)      /* live recognition period */
#define SUBMIT_MS       (1500U)     /* pen up time before sending a drawing */
#define PEN_SPEED       (0.35f)     /* d-pad, pixels per frame */
#define TILT_SPEED      (1.2f)      /* tilt, pixels per frame per g */
#define TILT_DEADZONE   (0.08f)
#define BRUSH           (1.6f)      /* brush radius in pixels */
/* the PyBadge buttons come with a d-pad, the CLUE is tilted instead */
#define HAVE_DPAD       IS_USED(MODULE_SN74HC165)

#define COLOR_BG        GFX_RGB(10, 12, 30)
#define COLOR_FRAME     GFX_RGB(60, 70, 120)
#define COLOR_INK       GFX_RGB(120, 255, 230)
#define COLOR_TEXT      GFX_RGB(220, 225, 255)
#define COLOR_DIM       GFX_RGB(110, 115, 150)
#define COLOR_ACCENT    GFX_RGB(255, 200, 70)
#define COLOR_GOOD      GFX_RGB(60, 220, 110)
#define COLOR_BAD       GFX_RGB(240, 70, 70)
#define COLOR_BAR       GFX_RGB(70, 120, 230)

typedef struct {
    int scale;          /* screen pixels per canvas pixel */
    int cx, cy;         /* canvas */
    int px, pw;         /* right panel */
    int digit_y, digit_scale;
    int info_y;
    int bars_x, bars_y, bars_w, bars_h;
    int bar_w;
} layout_t;

static layout_t _ui;
static uint8_t _canvas[DIGITS_PIXELS];
static uint8_t _norm[DIGITS_PIXELS];
static bool _dirty[DIGITS_PIXELS];
static bool _any_dirty;
static float _pen_x = DIGITS_SIZE / 2, _pen_y = DIGITS_SIZE / 2;
static int _cursor_x = -1, _cursor_y = -1;
static bool _from_pc;
static bool _have_motion;
static unsigned _pc_total, _pc_correct;
static buttons_t _buttons;

/* requests from the serial reader thread, handed over to the main thread */
static char _line[PROTO_LINE_MAX + 2];
static proto_request_t _req;
static volatile bool _pending;
static volatile bool _malformed;
static mutex_t _consumed = MUTEX_INIT_LOCKED;
static char _reader_stack[THREAD_STACKSIZE_DEFAULT];
static char _out[PROTO_LINE_MAX + 2];

static uint16_t _ink(uint8_t v)
{
    /* blend from the background to the ink color */
    unsigned r = 10 + (120 - 10) * v / 255;
    unsigned g = 12 + (255 - 12) * v / 255;
    unsigned b = 30 + (230 - 30) * v / 255;
    return GFX_RGB(r, g, b);
}

static void _layout(void)
{
    int w = gfx_width();
    int h = gfx_height();

    if (w >= 200 && h >= 200) {
        /* CLUE, 240x240: canvas left, digit right, bars below */
        _ui = (layout_t){
            .scale = 5, .cx = 8, .cy = 24, .px = 156, .pw = w - 160,
            .digit_y = 36, .digit_scale = 8, .info_y = 110,
            .bars_x = 8, .bars_y = 176, .bars_w = w - 16, .bars_h = 50,
        };
    }
    else {
        /* PyBadge, 160x128: canvas left, digit and bars right */
        _ui = (layout_t){
            .scale = 3, .cx = 3, .cy = 14, .px = 92, .pw = w - 94,
            .digit_y = 16, .digit_scale = 5, .info_y = 54,
            .bars_x = 92, .bars_y = 86, .bars_w = w - 94, .bars_h = 30,
        };
    }
    _ui.bar_w = _ui.bars_w / DIGITS_CLASSES;
}

static void _draw_cell(int x, int y)
{
    uint16_t color = _ink(_canvas[y * DIGITS_SIZE + x]);

    if (x == _cursor_x && y == _cursor_y) {
        color = COLOR_ACCENT;
    }
    gfx_fill_rect(_ui.cx + x * _ui.scale, _ui.cy + y * _ui.scale,
                  _ui.scale, _ui.scale, color);
}

static void _draw_canvas(void)
{
    static uint16_t row[DIGITS_SIZE * 5 * 5];
    int s = _ui.scale;

    for (int y = 0; y < DIGITS_SIZE; y++) {
        for (int x = 0; x < DIGITS_SIZE; x++) {
            uint16_t color = _ink(_canvas[y * DIGITS_SIZE + x]);
            for (int dy = 0; dy < s; dy++) {
                for (int dx = 0; dx < s; dx++) {
                    row[dy * DIGITS_SIZE * s + x * s + dx] = color;
                }
            }
        }
        gfx_pixels(_ui.cx, _ui.cy + y * s, DIGITS_SIZE * s, s, row);
    }
    memset(_dirty, 0, sizeof(_dirty));
    _any_dirty = false;
    _cursor_x = -1;
}

static void _draw_frame(void)
{
    int side = DIGITS_SIZE * _ui.scale;

    gfx_clear(COLOR_BG);
    gfx_text_centered(_ui.scale == 5 ? 4 : 2, "DIGIT ORACLE", COLOR_ACCENT,
                      COLOR_BG, _ui.scale == 5 ? 2 : 1);
    gfx_fill_rect(_ui.cx - 1, _ui.cy - 1, side + 2, 1, COLOR_FRAME);
    gfx_fill_rect(_ui.cx - 1, _ui.cy + side, side + 2, 1, COLOR_FRAME);
    gfx_fill_rect(_ui.cx - 1, _ui.cy, 1, side, COLOR_FRAME);
    gfx_fill_rect(_ui.cx + side, _ui.cy, 1, side, COLOR_FRAME);
    for (int i = 0; i < DIGITS_CLASSES; i++) {
        char label[2] = { '0' + i, '\0' };
        int x = _ui.bars_x + i * _ui.bar_w + (_ui.bar_w - GFX_CHAR_W) / 2 + 1;
        gfx_text(x, _ui.bars_y + _ui.bars_h + 2, label, COLOR_DIM, COLOR_BG, 1);
    }
    _draw_canvas();
}

static void _text_in_panel(int y, const char *text, uint16_t color,
                           unsigned scale)
{
    int w = gfx_text_width(text, scale);
    int x = _ui.px + (_ui.pw - w) / 2;

    gfx_fill_rect(_ui.px, y, _ui.pw, GFX_CHAR_H * scale, COLOR_BG);
    gfx_text(x < _ui.px ? _ui.px : x, y, text, color, COLOR_BG, scale);
}

/* the long text when it fits in the panel, else the short one */
static const char *_fit(const char *text, const char *short_text)
{
    return gfx_text_width(text, 1) <= _ui.pw ? text : short_text;
}

static void _show_result(int digit, const float proba[DIGITS_CLASSES],
                         int label, unsigned us)
{
    char text[24];
    char short_text[24];
    uint16_t color = COLOR_TEXT;

    if (label >= 0) {
        color = digit == label ? COLOR_GOOD : COLOR_BAD;
    }
    text[0] = digit < 0 ? '?' : '0' + digit;
    text[1] = '\0';
    _text_in_panel(_ui.digit_y, text, color, _ui.digit_scale);

    unsigned line = _ui.info_y;
    if (digit >= 0) {
        int percent = (int)(proba[digit] * 100 + 0.5f);
        snprintf(text, sizeof(text), "%d%% %uUS", percent, us);
        snprintf(short_text, sizeof(short_text), "%d%%", percent);
    }
    else {
        text[0] = '\0';
        short_text[0] = '\0';
    }
    _text_in_panel(line, _fit(text, short_text), COLOR_TEXT, 1);
    line += GFX_CHAR_H + 2;

    if (_from_pc) {
        if (label >= 0) {
            snprintf(text, sizeof(text), "WANTED %d", label);
        }
        else {
            snprintf(text, sizeof(text), "FROM PC");
        }
        _text_in_panel(line, text, COLOR_DIM, 1);
        line += GFX_CHAR_H + 2;
        snprintf(text, sizeof(text), "SCORE %u/%u", _pc_correct, _pc_total);
        snprintf(short_text, sizeof(short_text), "%u/%u", _pc_correct, _pc_total);
        _text_in_panel(line, _fit(text, short_text), COLOR_DIM, 1);
    }
    else {
        _text_in_panel(line, "DRAWN", COLOR_DIM, 1);
        line += GFX_CHAR_H + 2;
        _text_in_panel(line, "", COLOR_DIM, 1);
    }

    for (int i = 0; i < DIGITS_CLASSES; i++) {
        int h = digit < 0 ? 0 : (int)(proba[i] * _ui.bars_h + 0.5f);
        int x = _ui.bars_x + i * _ui.bar_w + 1;
        uint16_t bar = i == digit ? color : COLOR_BAR;
        gfx_fill_rect(x, _ui.bars_y, _ui.bar_w - 2, _ui.bars_h - h, COLOR_BG);
        gfx_fill_rect(x, _ui.bars_y + _ui.bars_h - h, _ui.bar_w - 2, h, bar);
    }
}

static void _clear(void)
{
    static const float none[DIGITS_CLASSES];

    memset(_canvas, 0, sizeof(_canvas));
    _from_pc = false;
    _draw_canvas();
    _show_result(-1, none, -1, 0);
}

static int _classify(const uint8_t *image, float proba[DIGITS_CLASSES],
                     unsigned *us)
{
    uint32_t start = ztimer_now(ZTIMER_USEC);
    int digit = digits_classify(image, proba);

    *us = ztimer_now(ZTIMER_USEC) - start;
    return digit;
}

/* recognize the drawing, false when the canvas is empty */
static bool _classify_drawing(bool submit)
{
    float proba[DIGITS_CLASSES];
    unsigned us;

    if (digits_normalize(_canvas, _norm) != 0) {
        return false;
    }
    int digit = _classify(_norm, proba, &us);
    _show_result(digit, proba, -1, us);
    if (submit) {
        size_t n = proto_format_image(_out, _norm);
        fwrite(_out, 1, n, stdout);
        n = proto_format_result(_out, sizeof(_out), digit, us, proba);
        fwrite(_out, 1, n, stdout);
        fflush(stdout);
    }
    return true;
}

/* answer a request, true when it replaced the canvas */
static bool _handle_request(void)
{
    float proba[DIGITS_CLASSES];
    unsigned us;

    if (_malformed) {
        puts("E malformed line");
        return false;
    }
    if (_req.cmd == '?') {
        puts("I digit-oracle " PROTO_VERSION);
        return false;
    }

    const uint8_t *image = _req.image;
    if (_req.cmd == 'N') {
        if (digits_normalize(_req.image, _norm) != 0) {
            puts("E empty image");
            return false;
        }
        image = _norm;
    }
    int digit = _classify(image, proba, &us);
    size_t n = proto_format_result(_out, sizeof(_out), digit, us, proba);
    fwrite(_out, 1, n, stdout);
    fflush(stdout);

    /* show what was received, normalized or not */
    memcpy(_canvas, image, DIGITS_PIXELS);
    _from_pc = true;
    if (_req.label >= 0) {
        _pc_total++;
        if (digit == _req.label) {
            _pc_correct++;
        }
        tone_beep(digit == _req.label ? NOTE_E6 : NOTE_C4, 40);
    }
    _draw_canvas();
    _show_result(digit, proba, _req.label, us);
    return true;
}

static void *_reader(void *arg)
{
    (void)arg;

    while (1) {
        size_t len = 0;
        bool overflow = false;
        int c;

        while ((c = getchar()) != '\n') {
            if (c == EOF) {
                /* only seen on native at the end of its stdin */
                ztimer_sleep(ZTIMER_MSEC, 100);
                continue;
            }
            if (c == '\r') {
                continue;
            }
            if (len < sizeof(_line) - 1) {
                _line[len++] = c;
            }
            else {
                overflow = true;
            }
        }
        if (len == 0) {
            continue;
        }
        _line[len] = '\0';
        _malformed = overflow || proto_parse(_line, &_req) != 0;
        _pending = true;
        /* wait until the main thread is done with the request */
        mutex_lock(&_consumed);
    }
    return NULL;
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
            float d = sqrtf(dx * dx + dy * dy);
            float v = (BRUSH - d) * 255.0f;
            v = v > 255.0f ? 255.0f : v;
            unsigned i = y * DIGITS_SIZE + x;
            if (v > _canvas[i]) {
                _canvas[i] = (uint8_t)v;
                _dirty[i] = true;
                _any_dirty = true;
            }
        }
    }
}

static void _move_pen(void)
{
    float vx = 0, vy = 0;

    if (_buttons.held & BTN_LEFT) {
        vx -= PEN_SPEED;
    }
    if (_buttons.held & BTN_RIGHT) {
        vx += PEN_SPEED;
    }
    if (_buttons.held & BTN_UP) {
        vy -= PEN_SPEED;
    }
    if (_buttons.held & BTN_DOWN) {
        vy += PEN_SPEED;
    }
    if (_have_motion && vx == 0 && vy == 0) {
        float tx, ty;
        if (accel_tilt(&tx, &ty)) {
            tx = fabsf(tx) < TILT_DEADZONE ? 0 : tx;
            ty = fabsf(ty) < TILT_DEADZONE ? 0 : ty;
            vx = tx * TILT_SPEED;
            vy = ty * TILT_SPEED;
        }
    }

    float x0 = _pen_x, y0 = _pen_y;
    _pen_x = fminf(fmaxf(_pen_x + vx, 0), DIGITS_SIZE - 0.01f);
    _pen_y = fminf(fmaxf(_pen_y + vy, 0), DIGITS_SIZE - 0.01f);

    if (_buttons.held & BTN_A) {
        /* fill the gaps of fast moves */
        float dist = hypotf(_pen_x - x0, _pen_y - y0);
        int steps = 1 + (int)(dist / 0.5f);
        for (int i = 1; i <= steps; i++) {
            _paint(x0 + (_pen_x - x0) * i / steps,
                   y0 + (_pen_y - y0) * i / steps);
        }
    }
}

static void _update_cursor(void)
{
    int x = (int)_pen_x;
    int y = (int)_pen_y;

    if (x == _cursor_x && y == _cursor_y) {
        return;
    }
    int old_x = _cursor_x, old_y = _cursor_y;
    _cursor_x = x;
    _cursor_y = y;
    if (old_x >= 0) {
        _draw_cell(old_x, old_y);
    }
    _draw_cell(x, y);
}

int main(void)
{
    uint32_t last_classify = 0;
    uint32_t last_ink = 0;
    bool submitted = true;

    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    if (HAVE_DPAD) {
        puts("Digit Oracle, the d-pad moves the pen");
    }
    else {
        _have_motion = accel_init(50) != 0;
        printf("Digit Oracle, %s\n", _have_motion ? "tilt to move the pen" :
               "no motion sensor");
    }
    puts("I digit-oracle " PROTO_VERSION);

    _layout();
    _draw_frame();
    _clear();

    thread_create(_reader_stack, sizeof(_reader_stack), THREAD_PRIORITY_MAIN - 1,
                  0, _reader, NULL, "reader");

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);

        if (_pending) {
            if (_handle_request()) {
                submitted = true;
            }
            _pending = false;
            mutex_unlock(&_consumed);
        }

        buttons_update(&_buttons);
        if (_buttons.pressed & BTN_SELECT) {
            tone_mute(!tone_is_muted());
        }
        if (_buttons.pressed & BTN_B) {
            _clear();
            submitted = true;
        }
        if ((_buttons.pressed & BTN_A) && _from_pc) {
            /* start a new drawing over a digit received from the PC */
            _clear();
        }

        _move_pen();
        if (_any_dirty) {
            for (unsigned i = 0; i < DIGITS_PIXELS; i++) {
                if (_dirty[i]) {
                    _dirty[i] = false;
                    _draw_cell(i % DIGITS_SIZE, i / DIGITS_SIZE);
                }
            }
            _any_dirty = false;
            last_ink = now;
            submitted = false;
        }
        _update_cursor();

        if (!_from_pc && !submitted) {
            if (now - last_classify >= CLASSIFY_MS) {
                last_classify = now;
                _classify_drawing(false);
            }
            if (!(_buttons.held & BTN_A) && now - last_ink >= SUBMIT_MS) {
                submitted = true;
                if (_classify_drawing(true)) {
                    tone_beep(NOTE_A5, 30);
                }
            }
        }

        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }
    return 0;
}
