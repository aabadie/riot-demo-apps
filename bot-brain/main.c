/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Bot Brain: Pong against a neural network
 *
 * The right paddle is played by a small emlearn neural network, trained in
 * Python by imitating a scripted player in a simulation of this very game
 * (tools/train.py). The bars show what the network wants to do, and on the
 * CLUE the 16 squares show its hidden neurons at work.
 *
 * First to 7 points wins. Win, and the bot gets faster.
 *
 * Controls:
 * - PyBadge: UP and DOWN move the paddle, A starts, SELECT toggles the sound
 * - CLUE: A moves up, B moves down, A or B starts
 */

#include <stdbool.h>
#include <stdio.h>

#include "random.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#include "pong.h"

#define FRAME_MS        (20U)
#define WIN_SCORE       (7)
#define LEVELS          (3)

#define COLOR_BG        GFX_RGB(6, 8, 18)
#define COLOR_FIELD     GFX_RGB(14, 18, 36)
#define COLOR_LINE      GFX_RGB(50, 60, 100)
#define COLOR_TEXT      GFX_RGB(225, 230, 255)
#define COLOR_DIM       GFX_RGB(110, 115, 150)
#define COLOR_PLAYER    GFX_RGB(90, 220, 255)
#define COLOR_BOT       GFX_RGB(255, 120, 80)
#define COLOR_BALL      GFX_RGB(255, 255, 255)

static const float _bot_speed[LEVELS] = { 1.0f, 1.3f, 1.6f };
static const char *const _level_names[LEVELS] = { "EASY", "MEDIUM", "HARD" };

static pong_t _game;
static int _fx, _fy;            /* field on the screen */
static bool _big;
static int _score_player, _score_bot;
static unsigned _level;
static buttons_t _buttons;
static float _proba[PONG_ACTIONS];

/* what is drawn, to erase it */
static int _drawn_bx = -100, _drawn_by;
static int _drawn_left = -1, _drawn_right = -1;

static void _field_rect(int x, int y, int w, int h, uint16_t color)
{
    /* clip to the field */
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > PONG_W) {
        w = PONG_W - x;
    }
    if (y + h > PONG_H) {
        h = PONG_H - y;
    }
    if (w > 0 && h > 0) {
        gfx_fill_rect(_fx + x, _fy + y, w, h, color);
    }
}

static void _center_line(int y0, int h)
{
    for (int y = 0; y < PONG_H; y += 8) {
        if (y + 4 > y0 && y < y0 + h) {
            _field_rect(PONG_W / 2 - 1, y, 2, 4, COLOR_LINE);
        }
    }
}

static void _score(void)
{
    char text[16];
    unsigned s = _big ? 2 : 1;
    int y = _fy - (_big ? 22 : 11);

    gfx_fill_rect(0, y, gfx_width(), GFX_CHAR_H * s, COLOR_BG);
    snprintf(text, sizeof(text), "%d", _score_player);
    gfx_text(_fx + PONG_W / 2 - 12 * s - gfx_text_width(text, s), y, text,
             COLOR_PLAYER, COLOR_BG, s);
    snprintf(text, sizeof(text), "%d", _score_bot);
    gfx_text(_fx + PONG_W / 2 + 12 * s, y, text, COLOR_BOT, COLOR_BG, s);
    gfx_text(_fx, y, _level_names[_level], COLOR_DIM, COLOR_BG, 1);
}

static void _draw_field(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(_fx, _fy, PONG_W, PONG_H, COLOR_FIELD);
    _center_line(0, PONG_H);
    _drawn_bx = -100;
    _drawn_left = _drawn_right = -1;
    _score();
    if (_big) {
        gfx_text(_fx, _fy + PONG_H + 6, "THE BOT WANTS TO", COLOR_DIM, COLOR_BG, 1);
        gfx_text(_fx, _fy + PONG_H + 44, "ITS HIDDEN NEURONS", COLOR_DIM,
                 COLOR_BG, 1);
    }
}

static void _draw_paddle(int x, float y, int *drawn, uint16_t color)
{
    int iy = (int)(y + 0.5f);

    if (iy == *drawn) {
        return;
    }
    if (*drawn >= 0) {
        _field_rect(x, *drawn, PONG_PADDLE_W, PONG_PADDLE_H, COLOR_FIELD);
    }
    _field_rect(x, iy, PONG_PADDLE_W, PONG_PADDLE_H, color);
    *drawn = iy;
}

static void _draw(void)
{
    int bx = (int)(_game.bx + 0.5f);
    int by = (int)(_game.by + 0.5f);

    if (bx != _drawn_bx || by != _drawn_by) {
        if (_drawn_bx > -100) {
            _field_rect(_drawn_bx, _drawn_by, PONG_BALL, PONG_BALL, COLOR_FIELD);
            if (_drawn_bx + PONG_BALL >= PONG_W / 2 - 1 && _drawn_bx <= PONG_W / 2 + 1) {
                _center_line(_drawn_by, PONG_BALL);
            }
        }
        _field_rect(bx, by, PONG_BALL, PONG_BALL, COLOR_BALL);
        _drawn_bx = bx;
        _drawn_by = by;
    }
    _draw_paddle(PONG_PADDLE_X, _game.left, &_drawn_left, COLOR_PLAYER);
    _draw_paddle(PONG_W - PONG_PADDLE_X - PONG_PADDLE_W, _game.right,
                 &_drawn_right, COLOR_BOT);
}

static void _draw_brain(void)
{
    static const char *const labels[PONG_ACTIONS] = { "UP", "STAY", "DOWN" };

    if (_big) {
        /* intentions as bars, then the hidden neurons */
        int y = _fy + PONG_H + 18;
        int bw = (PONG_W - 2 * 8) / PONG_ACTIONS;
        for (int i = 0; i < PONG_ACTIONS; i++) {
            int x = _fx + i * (bw + 8);
            int w = (int)(_proba[i] * (bw - 30));
            gfx_text(x, y + 2, labels[i], COLOR_DIM, COLOR_BG, 1);
            gfx_fill_rect(x + 30, y, w, 10, COLOR_BOT);
            gfx_fill_rect(x + 30 + w, y, bw - 30 - w, 10, COLOR_FIELD);
        }
        const float *hidden;
        unsigned n = pong_bot_hidden(&hidden);
        int cell = PONG_W / (int)n;
        for (unsigned i = 0; i < n; i++) {
            float v = hidden[i] / 3.0f;
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            unsigned c = (unsigned)(v * 255);
            gfx_fill_rect(_fx + (int)i * cell + 1, _fy + PONG_H + 56, cell - 2,
                          cell - 2, GFX_RGB(20 + c * 235 / 255, 20 + c * 100 / 255,
                                            40 + c * 40 / 255));
        }
    }
    else {
        /* intentions as three small bars next to the score */
        int x = gfx_width() - 3 * 8 - 2;
        for (int i = 0; i < PONG_ACTIONS; i++) {
            int h = (int)(_proba[i] * 9);
            gfx_fill_rect(x + i * 8, 1, 6, 9 - h, COLOR_BG);
            gfx_fill_rect(x + i * 8, 10 - h, 6, h, COLOR_BOT);
        }
    }
}

static void _message(const char *big, const char *small)
{
    unsigned s = _big ? 2 : 1;

    gfx_fill_rect(_fx, _fy + PONG_H / 2 - 14, PONG_W, 30, COLOR_FIELD);
    gfx_text_centered(_fy + PONG_H / 2 - 12, big, COLOR_TEXT, COLOR_FIELD, s);
    gfx_text_centered(_fy + PONG_H / 2 + 6, small, COLOR_DIM, COLOR_FIELD, 1);
}

static void _wait_start(void)
{
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    } while (!(_buttons.pressed & (BTN_A | BTN_B)));
}

static int _player_move(void)
{
    int up = _buttons.held & BTN_UP;
    int down = _buttons.held & BTN_DOWN;

    /* the CLUE has only A and B */
    if (!(_buttons.held & (BTN_UP | BTN_DOWN))) {
        up = _buttons.held & BTN_A;
        down = _buttons.held & BTN_B;
    }
    if (up && !down) {
        return PONG_UP;
    }
    if (down && !up) {
        return PONG_DOWN;
    }
    return PONG_STAY;
}

static void _play_game(void)
{
    unsigned frame = 0;

    _score_player = _score_bot = 0;
    pong_init(&_game, random_uint32() | 1, _bot_speed[_level]);
    _draw_field();
    _message("BOT BRAIN", "PRESS A TO PLAY");
    _wait_start();
    _draw_field();

    while (_score_player < WIN_SCORE && _score_bot < WIN_SCORE) {
        buttons_update(&_buttons);
        if (_buttons.pressed & BTN_SELECT) {
            tone_mute(!tone_is_muted());
        }
        int bot = pong_bot(&_game, _proba);
        int point = pong_step(&_game, _player_move(), bot);
        if (pong_hit(&_game)) {
            tone_beep(_game.vx > 0 ? NOTE_A5 : NOTE_E5, 20);
        }
        _draw();
        if (++frame % 3 == 0) {
            _draw_brain();
        }
        if (point) {
            if (point > 0) {
                _score_player++;
                tone_beep(NOTE_C6, 120);
            }
            else {
                _score_bot++;
                tone_beep(NOTE_C4, 120);
            }
            printf("score %d - %d\n", _score_player, _score_bot);
            _score();
            ztimer_sleep(ZTIMER_MSEC, 600);
            /* the ball goes to who lost the point */
            pong_serve(&_game, point > 0 ? 1 : -1);
        }
        ztimer_sleep(ZTIMER_MSEC, FRAME_MS);
    }

    bool won = _score_player >= WIN_SCORE;
    _message(won ? "YOU WIN" : "THE BOT WINS",
             won ? "THE BOT GETS FASTER" : "PRESS A TO PLAY AGAIN");
    tone_beep(won ? NOTE_E6 : NOTE_C4, 300);
    if (won && _level + 1 < LEVELS) {
        _level++;
    }
    ztimer_sleep(ZTIMER_MSEC, 1000);
    _wait_start();
}

int main(void)
{
    buttons_init();
    tone_init(board_speaker_init());
    if (gfx_init() != 0) {
        puts("no display found");
    }
    random_init(ztimer_now(ZTIMER_USEC));

    _big = gfx_width() >= 200;
    _fx = (gfx_width() - PONG_W) / 2;
    _fy = _big ? 36 : gfx_height() - PONG_H;
    _level = 1;
    puts("Bot Brain");

    while (1) {
        _play_game();
    }
    return 0;
}
