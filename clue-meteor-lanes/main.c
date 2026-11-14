/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Meteor Lanes: dodge the meteors with the two buttons
 *
 * The ship flies in one of three lanes. Button A moves it left, button B
 * moves it right. Meteors fall faster and faster; stars give bonus points.
 */

#include <stdbool.h>
#include <stdio.h>

#include "random.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#define FRAME_MS        (33U)
#define HUD_H           (20)
#define LANES           (3)
#define SPRITE_SCALE    (2)
#define MAX_OBJECTS     (12)
#define NUM_STARS       (24)
#define PLAYER_SLIDE    (4)     /* frames to move to the next lane */

/* Speeds are in 1/16 pixel per frame */
#define SPEED_START     (40)
#define SPEED_MAX       (150)

#define COLOR_BG        GFX_RGB(5, 5, 20)
#define COLOR_HUD       GFX_RGB(20, 20, 60)
#define COLOR_TEXT      GFX_RGB(230, 230, 255)
#define COLOR_ACCENT    GFX_RGB(255, 190, 40)
#define COLOR_DANGER    GFX_RGB(255, 70, 50)

static const char *const _ship_rows[] = {
    ".....w.....",
    "....wcw....",
    "....wcw....",
    "...wwcww...",
    "...wbbbw...",
    "..wwbbbww..",
    ".rwwwwwwwr.",
    "rrwwwwwwwrr",
    "rr.wwwww.rr",
    "r...www...r",
    "....o.o....",
    "....y.y....",
};

static const gfx_palette_t _ship_palette[] = {
    { 'w', GFX_RGB(220, 230, 240) },
    { 'c', GFX_RGB(80, 200, 255) },
    { 'b', GFX_RGB(40, 90, 200) },
    { 'r', GFX_RGB(230, 50, 60) },
    { 'o', GFX_RGB(255, 140, 0) },
    { 'y', GFX_RGB(255, 230, 80) },
};

static const gfx_sprite_t _ship = {
    _ship_rows, 11, 12, _ship_palette, ARRAY_SIZE(_ship_palette),
};

static const char *const _meteor_rows[] = {
    "....gggg....",
    "..ggGGGGgg..",
    ".gGGGdGGGGg.",
    ".gGGddGGGGg.",
    "gGGGGGGGdGGg",
    "gGGGGGGddGGg",
    "gGdGGGGGGGGg",
    "gGddGGGGGGGg",
    ".gGGGGGdGGg.",
    "..ggGGGGgg..",
    "....gggg....",
};

static const gfx_palette_t _meteor_palette[] = {
    { 'g', GFX_RGB(110, 70, 50) },
    { 'G', GFX_RGB(160, 110, 80) },
    { 'd', GFX_RGB(80, 50, 35) },
};

static const gfx_sprite_t _meteor = {
    _meteor_rows, 12, 11, _meteor_palette, ARRAY_SIZE(_meteor_palette),
};

static const char *const _star_rows[] = {
    "...y...",
    "..yYy..",
    ".yYWYy.",
    "yYWWWYy",
    ".yYWYy.",
    "..yYy..",
    "...y...",
};

static const gfx_palette_t _star_palette[] = {
    { 'y', GFX_RGB(200, 150, 0) },
    { 'Y', GFX_RGB(255, 210, 40) },
    { 'W', GFX_RGB(255, 255, 200) },
};

static const gfx_sprite_t _star = {
    _star_rows, 7, 7, _star_palette, ARRAY_SIZE(_star_palette),
};

typedef enum {
    OBJ_NONE,
    OBJ_METEOR,
    OBJ_STAR,
} obj_type_t;

typedef struct {
    obj_type_t type;
    int lane;
    int y16;        /* top position in 1/16 pixel */
    int drawn_y;    /* top position drawn on screen */
} object_t;

typedef struct {
    int x;
    int y16;
    int drawn_y;
    int speed;      /* in 1/16 pixel per frame */
} dust_t;

static object_t _objects[MAX_OBJECTS];
static dust_t _dust[NUM_STARS];
static buttons_t _buttons;

static int _width;
static int _height;
static int _lane_w;
static int _player_y;
static int _player_x;
static int _player_drawn_x;
static int _player_lane;
static int _player_from_x;
static int _slide;
static int _speed;
static int _next_row16;
static unsigned _score;
static unsigned _best;
static unsigned _shown_score = ~0U;

static const tone_note_t _start_jingle[] = {
    { NOTE_C5, 70 }, { NOTE_E5, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 140 },
};

static const tone_note_t _star_sound[] = {
    { NOTE_E6, 40 }, { NOTE_G6, 40 }, { NOTE_A6, 60 },
};

static const tone_note_t _crash_sound[] = {
    { NOTE_A4, 60 }, { NOTE_F4, 60 }, { NOTE_D4, 80 }, { NOTE_A3, 120 },
    { NOTE_REST, 40 }, { NOTE_G3, 250 },
};

/* An original little tune for the title screen */
static const tone_note_t _title_tune[] = {
    { NOTE_C5, 150 }, { NOTE_G4, 150 }, { NOTE_C5, 150 }, { NOTE_E5, 150 },
    { NOTE_D5, 300 }, { NOTE_REST, 150 }, { NOTE_G4, 150 },
    { NOTE_A4, 150 }, { NOTE_C5, 150 }, { NOTE_E5, 150 }, { NOTE_G5, 150 },
    { NOTE_E5, 450 }, { NOTE_REST, 150 },
    { NOTE_F5, 150 }, { NOTE_E5, 150 }, { NOTE_D5, 150 }, { NOTE_C5, 150 },
    { NOTE_D5, 300 }, { NOTE_G4, 300 },
    { NOTE_C5, 600 }, { NOTE_REST, 600 },
};

static int _lane_x(int lane, int sprite_w)
{
    return lane * _lane_w + (_lane_w - sprite_w) / 2;
}

static const gfx_sprite_t *_sprite_of(obj_type_t type)
{
    return (type == OBJ_STAR) ? &_star : &_meteor;
}

static void _draw_hud(void)
{
    char buf[24];

    if (_score == _shown_score) {
        return;
    }
    _shown_score = _score;
    snprintf(buf, sizeof(buf), "%05u", _score);
    gfx_text(4, 2, buf, COLOR_TEXT, COLOR_HUD, 2);
    snprintf(buf, sizeof(buf), "HI %05u", _best);
    gfx_text(_width - gfx_text_width(buf, 2) - 4, 2, buf, COLOR_ACCENT, COLOR_HUD, 2);
}

static void _reset(void)
{
    for (unsigned i = 0; i < MAX_OBJECTS; i++) {
        _objects[i].type = OBJ_NONE;
    }
    for (unsigned i = 0; i < NUM_STARS; i++) {
        _dust[i].x = random_uint32_range(0, _width);
        _dust[i].y16 = random_uint32_range(HUD_H, _height) * 16;
        _dust[i].drawn_y = -1;
        _dust[i].speed = random_uint32_range(8, 40);
    }
    _player_lane = 1;
    _player_x = _lane_x(_player_lane, _ship.width * SPRITE_SCALE);
    _player_drawn_x = -1;
    _slide = 0;
    _speed = SPEED_START;
    _next_row16 = 0;
    _score = 0;
    _shown_score = ~0U;

    gfx_fill_rect(0, 0, _width, HUD_H, COLOR_HUD);
    gfx_fill_rect(0, HUD_H, _width, _height - HUD_H, COLOR_BG);
}

static void _spawn(obj_type_t type, int lane)
{
    for (unsigned i = 0; i < MAX_OBJECTS; i++) {
        if (_objects[i].type == OBJ_NONE) {
            const gfx_sprite_t *sprite = _sprite_of(type);
            _objects[i].type = type;
            _objects[i].lane = lane;
            _objects[i].y16 = (HUD_H - sprite->height * SPRITE_SCALE) * 16;
            _objects[i].drawn_y = -1000;
            return;
        }
    }
}

/* Spawn a row with one or two meteors, never three, and maybe a star */
static void _spawn_row(void)
{
    int blocked = random_uint32_range(0, LANES);
    _spawn(OBJ_METEOR, blocked);

    if (_score > 10 && random_uint32_range(0, 100) < 45) {
        int second = (blocked + 1 + random_uint32_range(0, LANES - 1)) % LANES;
        _spawn(OBJ_METEOR, second);
        blocked = blocked + second;  /* sum of two distinct lanes in 0..2 */
        int free_lane = 3 - blocked;
        if (random_uint32_range(0, 100) < 25) {
            _spawn(OBJ_STAR, free_lane);
        }
    }
    else if (random_uint32_range(0, 100) < 20) {
        _spawn(OBJ_STAR, (blocked + 1 + random_uint32_range(0, LANES - 1)) % LANES);
    }
}

static void _draw_dust(void)
{
    for (unsigned i = 0; i < NUM_STARS; i++) {
        dust_t *d = &_dust[i];
        int y = d->y16 / 16;
        if (y == d->drawn_y) {
            continue;
        }
        if (d->drawn_y >= HUD_H) {
            gfx_fill_rect(d->x, d->drawn_y, 1, 1, COLOR_BG);
        }
        if (y >= HUD_H) {
            uint8_t level = 60 + d->speed * 4;
            gfx_fill_rect(d->x, y, 1, 1, GFX_RGB(level, level, level));
        }
        d->drawn_y = y;
    }
}

static void _move_dust(void)
{
    for (unsigned i = 0; i < NUM_STARS; i++) {
        dust_t *d = &_dust[i];
        d->y16 += d->speed * _speed / SPEED_START;
        if (d->y16 / 16 >= _height) {
            d->y16 = HUD_H * 16;
            d->x = random_uint32_range(0, _width);
        }
    }
}

/* Move the objects, return true on a crash */
static bool _move_objects(void)
{
    const int ship_w = _ship.width * SPRITE_SCALE;
    const int ship_h = _ship.height * SPRITE_SCALE;
    bool crash = false;

    for (unsigned i = 0; i < MAX_OBJECTS; i++) {
        object_t *obj = &_objects[i];
        if (obj->type == OBJ_NONE) {
            continue;
        }
        const gfx_sprite_t *sprite = _sprite_of(obj->type);
        const int w = sprite->width * SPRITE_SCALE;
        const int h = sprite->height * SPRITE_SCALE;
        const int x = _lane_x(obj->lane, w);

        obj->y16 += _speed;
        int y = obj->y16 / 16;

        /* erase the band left above the object */
        int top = (obj->drawn_y < HUD_H) ? HUD_H : obj->drawn_y;
        if (obj->drawn_y > -1000 && y > top) {
            gfx_fill_rect(x, top, w, y - top, COLOR_BG);
        }

        if (y >= _height) {
            if (obj->type == OBJ_METEOR) {
                _score++;
            }
            obj->type = OBJ_NONE;
            continue;
        }

        /* collision with the ship, with a margin of 3 pixels */
        bool overlap = (x + w - 3 > _player_x + 3) && (x + 3 < _player_x + ship_w - 3) &&
                       (y + h - 3 > _player_y + 3) && (y + 3 < _player_y + ship_h - 3);
        if (overlap) {
            if (obj->type == OBJ_STAR) {
                gfx_fill_rect(x, (y < HUD_H) ? HUD_H : y, w, h, COLOR_BG);
                obj->type = OBJ_NONE;
                _score += 5;
                tone_effect(_star_sound, ARRAY_SIZE(_star_sound));
                continue;
            }
            crash = true;
        }

        /* objects appear once they are fully below the HUD */
        if (y >= HUD_H) {
            gfx_sprite(x, y, sprite, SPRITE_SCALE, COLOR_BG);
        }
        obj->drawn_y = (y >= HUD_H) ? y : -1000;
    }
    return crash;
}

static void _move_player(void)
{
    const int ship_w = _ship.width * SPRITE_SCALE;
    const int ship_h = _ship.height * SPRITE_SCALE;

    if (_buttons.pressed & BTN_A && _player_lane > 0) {
        _player_from_x = _player_x;
        _player_lane--;
        _slide = PLAYER_SLIDE;
        tone_beep(NOTE_C6, 15);
    }
    if (_buttons.pressed & BTN_B && _player_lane < LANES - 1) {
        _player_from_x = _player_x;
        _player_lane++;
        _slide = PLAYER_SLIDE;
        tone_beep(NOTE_C6, 15);
    }

    int target = _lane_x(_player_lane, ship_w);
    if (_slide > 0) {
        _slide--;
        _player_x = target + (_player_from_x - target) * _slide / PLAYER_SLIDE;
    }
    else {
        _player_x = target;
    }

    if (_player_drawn_x >= 0 && _player_drawn_x != _player_x) {
        /* erase the uncovered side */
        if (_player_x > _player_drawn_x) {
            gfx_fill_rect(_player_drawn_x, _player_y, _player_x - _player_drawn_x, ship_h,
                          COLOR_BG);
        }
        else {
            gfx_fill_rect(_player_x + ship_w, _player_y, _player_drawn_x - _player_x, ship_h,
                          COLOR_BG);
        }
    }
    gfx_sprite(_player_x, _player_y, &_ship, SPRITE_SCALE, COLOR_BG);
    _player_drawn_x = _player_x;
}

static void _wait_any_button(void)
{
    /* wait for a release first so a held button does not skip the screen */
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (_buttons.held);
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (!_buttons.pressed);
}

static void _title_screen(void)
{
    gfx_clear(COLOR_BG);
    gfx_text_centered(40, "METEOR", COLOR_ACCENT, COLOR_BG, 4);
    gfx_text_centered(76, "LANES", COLOR_TEXT, COLOR_BG, 4);
    gfx_sprite((_width - _ship.width * 4) / 2, 115, &_ship, 4, COLOR_BG);
    gfx_text_centered(178, "A: LEFT   B: RIGHT", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(192, "STARS GIVE +5", COLOR_ACCENT, COLOR_BG, 1);
    gfx_text_centered(214, "PRESS A OR B", COLOR_TEXT, COLOR_BG, 2);

    tone_play(_title_tune, ARRAY_SIZE(_title_tune), true);
    _wait_any_button();
    tone_stop();
}

static void _game_over(void)
{
    bool record = _score > _best;
    char buf[24];

    tone_effect(_crash_sound, ARRAY_SIZE(_crash_sound));

    /* flash the ship */
    for (int i = 0; i < 4; i++) {
        gfx_fill_rect(_player_x, _player_y, _ship.width * SPRITE_SCALE,
                      _ship.height * SPRITE_SCALE, COLOR_DANGER);
        ztimer_sleep(ZTIMER_MSEC, 90);
        gfx_sprite(_player_x, _player_y, &_ship, SPRITE_SCALE, COLOR_BG);
        ztimer_sleep(ZTIMER_MSEC, 90);
    }

    if (record) {
        _best = _score;
    }

    gfx_fill_rect(20, 70, _width - 40, 100, COLOR_HUD);
    gfx_text_centered(82, "CRASH!", COLOR_DANGER, COLOR_HUD, 3);
    snprintf(buf, sizeof(buf), "SCORE %u", _score);
    gfx_text_centered(116, buf, COLOR_TEXT, COLOR_HUD, 2);
    gfx_text_centered(142, record ? "NEW RECORD!" : "PRESS A OR B",
                      record ? COLOR_ACCENT : COLOR_TEXT, COLOR_HUD, record ? 2 : 1);
    ztimer_sleep(ZTIMER_MSEC, 600);
    _wait_any_button();
}

static void _play(void)
{
    _reset();
    tone_effect(_start_jingle, ARRAY_SIZE(_start_jingle));

    uint32_t last = ztimer_now(ZTIMER_MSEC);
    while (1) {
        buttons_update(&_buttons);

        _move_dust();
        _draw_dust();

        _next_row16 -= _speed;
        if (_next_row16 <= 0) {
            _spawn_row();
            /* the gap between rows shrinks a little as it speeds up */
            _next_row16 = (110 - (_speed - SPEED_START) / 3) * 16;
        }

        bool crash = _move_objects();
        _move_player();
        _draw_hud();

        if (crash) {
            return;
        }

        if (_speed < SPEED_MAX) {
            _speed = SPEED_START + _score * 3 / 2;
            if (_speed > SPEED_MAX) {
                _speed = SPEED_MAX;
            }
        }

        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, FRAME_MS);
    }
}

int main(void)
{
    if (gfx_init() != 0) {
        puts("No display found");
        return 1;
    }
    buttons_init();
    tone_init(board_speaker_init());

    _width = gfx_width();
    _height = gfx_height();
    _lane_w = _width / LANES;
    _player_y = _height - _ship.height * SPRITE_SCALE - 8;

    while (1) {
        _title_screen();
        random_init(ztimer_now(ZTIMER_USEC));
        _play();
        _game_over();
    }

    return 0;
}
