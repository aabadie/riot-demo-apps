/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Tilt Maze: roll a ball to the green hole by tilting the PyBadge
 *
 * Each level is a new random maze, a bit larger. From level 3, red holes
 * send the ball back to the start. B sets the current position as flat,
 * START pauses, SELECT toggles the sound.
 *
 * The accelerometer is read with the lis3dh driver, on I2C on the PyBadge.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lis3dh.h"
#include "lis3dh_params.h"
#include "random.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

/* Accelerometer axes to screen axes, change them if the ball rolls the
   wrong way */
#ifndef TILT_SWAP_XY
#define TILT_SWAP_XY    (0)
#endif
#ifndef TILT_SIGN_X
#define TILT_SIGN_X     (1)
#endif
#ifndef TILT_SIGN_Y
#define TILT_SIGN_Y     (1)
#endif

#define FRAME_MS        (20U)
#define HUD_H           (10)
#define WALL            (2)
#define MAX_COLS        (16)
#define MAX_ROWS        (12)
#define MAX_WALLS       ((MAX_COLS + 1) * MAX_ROWS + (MAX_ROWS + 1) * MAX_COLS)
#define MAX_TRAPS       (6)

#define ACCEL           (0.30f)     /* pixels per frame^2 per g */
#define FRICTION        (0.985f)
#define BOUNCE          (0.35f)
#define MAX_SPEED       (5.0f)

#define COLOR_BG        GFX_RGB(16, 18, 40)
#define COLOR_FLOOR     GFX_RGB(32, 36, 70)
#define COLOR_WALL      GFX_RGB(150, 170, 230)
#define COLOR_BALL      GFX_RGB(240, 240, 250)
#define COLOR_BALL_DARK GFX_RGB(150, 150, 170)
#define COLOR_GOAL      GFX_RGB(60, 220, 110)
#define COLOR_TRAP      GFX_RGB(230, 60, 60)
#define COLOR_HOLE      GFX_RGB(5, 5, 10)
#define COLOR_TEXT      GFX_RGB(220, 225, 255)
#define COLOR_ACCENT    GFX_RGB(255, 200, 70)

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8, VISITED = 16 };

typedef struct {
    int16_t x, y, w, h;
} rect_t;

typedef struct {
    int cx, cy;     /* cell */
    int x, y;       /* center in pixels */
} hole_t;

static uint8_t _cells[MAX_ROWS][MAX_COLS];
static rect_t _walls[MAX_WALLS];
static unsigned _num_walls;
static hole_t _goal;
static hole_t _traps[MAX_TRAPS];
static unsigned _num_traps;

static int _cols, _rows, _cell, _ox, _oy;
static int _radius;
static int _hole_r;

static float _bx, _by, _vx, _vy;
static int _drawn_x = -1000, _drawn_y = -1000;

static lis3dh_t _accel;
static bool _have_accel;
static float _zero_x, _zero_y;

static buttons_t _buttons;
static unsigned _level = 1;
static uint32_t _penalty_ms;

static uint16_t _ball_px[17 * 17];
static uint16_t _hole_px[2][17 * 17];

static const tone_note_t _sfx_win[] = {
    { NOTE_C5, 80 }, { NOTE_E5, 80 }, { NOTE_G5, 80 }, { NOTE_C6, 80 },
    { NOTE_G5, 80 }, { NOTE_C6, 240 },
};

static const tone_note_t _sfx_fall[] = {
    { NOTE_A5, 50 }, { NOTE_F5, 50 }, { NOTE_D5, 50 }, { NOTE_B4, 50 },
    { NOTE_G4, 50 }, { NOTE_E4, 50 }, { NOTE_C4, 150 },
};

static bool _accel_init(void)
{
    if (lis3dh_init(&_accel, &lis3dh_params[0]) != 0 ||
        lis3dh_set_odr(&_accel, LIS3DH_ODR_100Hz) != 0 ||
        lis3dh_set_scale(&_accel, 2) != 0) {
        puts("LIS3DH not found, the ball will not move");
        return false;
    }
    return true;
}

/* Read the tilt in g along the screen axes, x to the right, y down */
static void _accel_read(float *gx, float *gy)
{
    lis3dh_data_t data;

    *gx = 0;
    *gy = 0;
    if (!_have_accel || lis3dh_read_xyz(&_accel, &data) != 0) {
        return;
    }

    float ax = data.acc_x / 1000.0f;
    float ay = data.acc_y / 1000.0f;

    if (TILT_SWAP_XY) {
        float tmp = ax;
        ax = ay;
        ay = tmp;
    }
    *gx = TILT_SIGN_X * ax - _zero_x;
    *gy = TILT_SIGN_Y * ay - _zero_y;
}

static void _calibrate(void)
{
    float sx = 0, sy = 0;
    float gx, gy;

    _zero_x = 0;
    _zero_y = 0;
    for (int i = 0; i < 8; i++) {
        _accel_read(&gx, &gy);
        sx += gx;
        sy += gy;
        ztimer_sleep(ZTIMER_MSEC, 12);
    }
    _zero_x = sx / 8;
    _zero_y = sy / 8;
    tone_beep(NOTE_A5, 40);
}

/* Disc sprites: the ball with a shine, the goal and the trap holes */
static void _build_sprites(void)
{
    int d = 2 * _radius + 1;
    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            int dx = x - _radius, dy = y - _radius;
            int r2 = dx * dx + dy * dy;
            uint16_t c = COLOR_FLOOR;
            if (r2 <= _radius * _radius) {
                c = (dx + dy > _radius / 2) ? COLOR_BALL_DARK : COLOR_BALL;
                if (dx == -_radius / 2 && dy == -_radius / 2) {
                    c = GFX_RGB(255, 255, 255);
                }
            }
            _ball_px[y * d + x] = c;
        }
    }

    int hd = 2 * _hole_r + 1;
    for (int y = 0; y < hd; y++) {
        for (int x = 0; x < hd; x++) {
            int dx = x - _hole_r, dy = y - _hole_r;
            int r2 = dx * dx + dy * dy;
            int inner = _hole_r - 2;
            uint16_t goal = COLOR_FLOOR, trap = COLOR_FLOOR;
            if (r2 <= _hole_r * _hole_r) {
                goal = COLOR_GOAL;
                trap = COLOR_TRAP;
            }
            if (r2 <= inner * inner) {
                goal = GFX_RGB(20, 90, 40);
                trap = COLOR_HOLE;
            }
            _hole_px[0][y * hd + x] = goal;
            _hole_px[1][y * hd + x] = trap;
        }
    }
}

static void _cell_center(int cx, int cy, int *x, int *y)
{
    *x = _ox + cx * _cell + (_cell + WALL) / 2;
    *y = _oy + cy * _cell + (_cell + WALL) / 2;
}

/* Carve a perfect maze with an iterative depth first search */
static void _generate(void)
{
    static uint8_t stack[MAX_COLS * MAX_ROWS];
    static const int dx[] = { 0, 1, 0, -1 };
    static const int dy[] = { -1, 0, 1, 0 };
    static const uint8_t wall[] = { WALL_N, WALL_E, WALL_S, WALL_W };
    static const uint8_t opposite[] = { WALL_S, WALL_W, WALL_N, WALL_E };
    unsigned top = 0;

    for (int y = 0; y < _rows; y++) {
        for (int x = 0; x < _cols; x++) {
            _cells[y][x] = WALL_N | WALL_E | WALL_S | WALL_W;
        }
    }

    _cells[0][0] |= VISITED;
    stack[top++] = 0;
    while (top) {
        int cur = stack[top - 1];
        int cx = cur % _cols, cy = cur / _cols;
        int options[4], n = 0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < _cols && ny >= 0 && ny < _rows && !(_cells[ny][nx] & VISITED)) {
                options[n++] = d;
            }
        }
        if (n == 0) {
            top--;
            continue;
        }
        int d = options[random_uint32_range(0, n)];
        int nx = cx + dx[d], ny = cy + dy[d];
        _cells[cy][cx] &= ~wall[d];
        _cells[ny][nx] &= ~opposite[d];
        _cells[ny][nx] |= VISITED;
        stack[top++] = (uint8_t)(ny * _cols + nx);
    }

    /* the wall rectangles, shared edges only once */
    _num_walls = 0;
    for (int y = 0; y < _rows; y++) {
        for (int x = 0; x < _cols; x++) {
            int px = _ox + x * _cell, py = _oy + y * _cell;
            if (_cells[y][x] & WALL_N) {
                _walls[_num_walls++] = (rect_t){ px, py, _cell + WALL, WALL };
            }
            if (_cells[y][x] & WALL_W) {
                _walls[_num_walls++] = (rect_t){ px, py, WALL, _cell + WALL };
            }
            if (y == _rows - 1 && (_cells[y][x] & WALL_S)) {
                _walls[_num_walls++] = (rect_t){ px, py + _cell, _cell + WALL, WALL };
            }
            if (x == _cols - 1 && (_cells[y][x] & WALL_E)) {
                _walls[_num_walls++] = (rect_t){ px + _cell, py, WALL, _cell + WALL };
            }
        }
    }

    _goal.cx = _cols - 1;
    _goal.cy = _rows - 1;
    _cell_center(_goal.cx, _goal.cy, &_goal.x, &_goal.y);

    _num_traps = 0;
    unsigned wanted = (_level >= 3) ? _level - 2 : 0;
    if (wanted > MAX_TRAPS) {
        wanted = MAX_TRAPS;
    }
    for (unsigned tries = 0; _num_traps < wanted && tries < 100; tries++) {
        int cx = random_uint32_range(0, _cols), cy = random_uint32_range(0, _rows);
        /* keep the start area and the goal free */
        if ((cx < 2 && cy < 2) || (cx == _goal.cx && cy == _goal.cy)) {
            continue;
        }
        bool used = false;
        for (unsigned i = 0; i < _num_traps; i++) {
            used |= (_traps[i].cx == cx && _traps[i].cy == cy);
        }
        if (used) {
            continue;
        }
        _traps[_num_traps].cx = cx;
        _traps[_num_traps].cy = cy;
        _cell_center(cx, cy, &_traps[_num_traps].x, &_traps[_num_traps].y);
        _num_traps++;
    }
}

static void _setup_level(void)
{
    static const uint8_t sizes[] = { 20, 16, 13, 12, 11, 10 };
    unsigned idx = (_level - 1 < ARRAY_SIZE(sizes)) ? _level - 1 : ARRAY_SIZE(sizes) - 1;

    _cell = sizes[idx];
    _cols = (gfx_width() - WALL) / _cell;
    _rows = (gfx_height() - HUD_H - WALL) / _cell;
    if (_cols > MAX_COLS) {
        _cols = MAX_COLS;
    }
    if (_rows > MAX_ROWS) {
        _rows = MAX_ROWS;
    }
    _ox = (gfx_width() - _cols * _cell - WALL) / 2;
    _oy = HUD_H + (gfx_height() - HUD_H - _rows * _cell - WALL) / 2;
    _radius = (_cell - WALL) / 2 - 2;
    if (_radius > 8) {
        _radius = 8;
    }
    if (_radius < 2) {
        _radius = 2;
    }
    _hole_r = _radius + 1;
    if (_hole_r > (_cell - WALL) / 2 - 1) {
        _hole_r = (_cell - WALL) / 2 - 1;
    }
    if (_hole_r > 8) {
        /* the sprite buffers hold 17 x 17 pixels */
        _hole_r = 8;
    }
    _build_sprites();
    _generate();
}

static void _draw_hole(const hole_t *hole, int kind)
{
    int d = 2 * _hole_r + 1;
    gfx_pixels(hole->x - _hole_r, hole->y - _hole_r, d, d, _hole_px[kind]);
}

static void _draw_maze(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(_ox, _oy, _cols * _cell + WALL, _rows * _cell + WALL, COLOR_FLOOR);
    for (unsigned i = 0; i < _num_walls; i++) {
        gfx_fill_rect(_walls[i].x, _walls[i].y, _walls[i].w, _walls[i].h, COLOR_WALL);
    }
    _draw_hole(&_goal, 0);
    for (unsigned i = 0; i < _num_traps; i++) {
        _draw_hole(&_traps[i], 1);
    }
    _drawn_x = -1000;
}

static bool _hits_wall(float x, float y)
{
    int x1 = (int)lroundf(x) - _radius, x2 = (int)lroundf(x) + _radius;
    int y1 = (int)lroundf(y) - _radius, y2 = (int)lroundf(y) + _radius;
    for (unsigned i = 0; i < _num_walls; i++) {
        const rect_t *r = &_walls[i];
        if (x2 >= r->x && x1 < r->x + r->w && y2 >= r->y && y1 < r->y + r->h) {
            return true;
        }
    }
    return false;
}

static bool _near(const hole_t *hole, int dist)
{
    float dx = _bx - hole->x, dy = _by - hole->y;
    return dx * dx + dy * dy <= (float)(dist * dist);
}

static void _draw_ball(void)
{
    int x = (int)lroundf(_bx), y = (int)lroundf(_by);
    int d = 2 * _radius + 1;

    if (x == _drawn_x && y == _drawn_y) {
        return;
    }
    if (_drawn_x > -1000) {
        gfx_fill_rect(_drawn_x - _radius, _drawn_y - _radius, d, d, COLOR_FLOOR);
        /* repair the holes the erased square may have touched */
        if (abs(_drawn_x - _goal.x) <= d && abs(_drawn_y - _goal.y) <= d) {
            _draw_hole(&_goal, 0);
        }
        for (unsigned i = 0; i < _num_traps; i++) {
            if (abs(_drawn_x - _traps[i].x) <= d && abs(_drawn_y - _traps[i].y) <= d) {
                _draw_hole(&_traps[i], 1);
            }
        }
    }
    gfx_pixels(x - _radius, y - _radius, d, d, _ball_px);
    _drawn_x = x;
    _drawn_y = y;
}

static void _reset_ball(void)
{
    int x, y;
    _cell_center(0, 0, &x, &y);
    _bx = x;
    _by = y;
    _vx = 0;
    _vy = 0;
}

/* Move one axis in steps of at most one pixel, bounce on the walls */
static bool _move_axis(float *pos, float *vel, bool is_x)
{
    float remaining = *vel;
    bool bumped = false;

    while (fabsf(remaining) > 0.0f) {
        float step = (remaining > 1.0f) ? 1.0f : ((remaining < -1.0f) ? -1.0f : remaining);
        float next = *pos + step;
        bool hit = is_x ? _hits_wall(next, _by) : _hits_wall(_bx, next);
        if (hit) {
            bumped = fabsf(*vel) > 1.2f;
            *vel = -*vel * BOUNCE;
            return bumped;
        }
        *pos = next;
        remaining -= step;
    }
    return bumped;
}

static void _draw_hud(uint32_t elapsed_ms)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "LEVEL %u", _level);
    gfx_text(2, 1, buf, COLOR_ACCENT, COLOR_BG, 1);
    snprintf(buf, sizeof(buf), "%3lu.%lu S", (unsigned long)(elapsed_ms / 1000),
             (unsigned long)((elapsed_ms / 100) % 10));
    gfx_text(gfx_width() - gfx_text_width(buf, 1) - 2, 1, buf, COLOR_TEXT, COLOR_BG, 1);
}

static void _wait_press(uint8_t mask)
{
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (_buttons.held);
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (!(_buttons.pressed & mask));
}

static void _fall_animation(const hole_t *hole)
{
    tone_effect(_sfx_fall, ARRAY_SIZE(_sfx_fall));
    int d = 2 * _radius + 1;
    gfx_fill_rect(_drawn_x - _radius, _drawn_y - _radius, d, d, COLOR_FLOOR);
    for (int r = _radius; r >= 0; r--) {
        _draw_hole(hole, 1);
        gfx_fill_rect(hole->x - r, hole->y - r, 2 * r + 1, 2 * r + 1, COLOR_BALL_DARK);
        ztimer_sleep(ZTIMER_MSEC, 60);
    }
    _draw_hole(hole, 1);
    _drawn_x = -1000;
}

/* Play one level, return the time spent in ms */
static uint32_t _play_level(void)
{
    _setup_level();
    _draw_maze();
    _reset_ball();
    _penalty_ms = 0;

    uint32_t start = ztimer_now(ZTIMER_MSEC);
    uint32_t last = start;
    uint32_t hud_update = 0;

    while (1) {
        buttons_update(&_buttons);
        if (_buttons.pressed & BTN_B) {
            _calibrate();
        }
        if (_buttons.pressed & BTN_SELECT) {
            tone_mute(!tone_is_muted());
        }
        if (_buttons.pressed & BTN_START) {
            uint32_t paused = ztimer_now(ZTIMER_MSEC);
            gfx_text_centered(1, " PAUSE ", COLOR_BG, COLOR_ACCENT, 1);
            _wait_press(BTN_START);
            gfx_fill_rect(0, 0, gfx_width(), HUD_H, COLOR_BG);
            start += ztimer_now(ZTIMER_MSEC) - paused;
            last = ztimer_now(ZTIMER_MSEC);
        }

        float gx, gy;
        _accel_read(&gx, &gy);
        _vx = (_vx + gx * ACCEL) * FRICTION;
        _vy = (_vy + gy * ACCEL) * FRICTION;
        _vx = fmaxf(-MAX_SPEED, fminf(MAX_SPEED, _vx));
        _vy = fmaxf(-MAX_SPEED, fminf(MAX_SPEED, _vy));

        bool bump = _move_axis(&_bx, &_vx, true);
        bump |= _move_axis(&_by, &_vy, false);
        if (bump) {
            tone_beep(NOTE_C4, 10);
        }

        _draw_ball();

        uint32_t now = ztimer_now(ZTIMER_MSEC);
        uint32_t elapsed = now - start + _penalty_ms;

        for (unsigned i = 0; i < _num_traps; i++) {
            if (_near(&_traps[i], _hole_r - 1)) {
                _fall_animation(&_traps[i]);
                _penalty_ms += 3000;
                _reset_ball();
                last = ztimer_now(ZTIMER_MSEC);
                break;
            }
        }

        if (_near(&_goal, _hole_r - 1)) {
            tone_effect(_sfx_win, ARRAY_SIZE(_sfx_win));
            return elapsed;
        }

        if (now - hud_update >= 100) {
            _draw_hud(elapsed);
            hud_update = now;
        }

        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, FRAME_MS);
    }
}

static void _title_screen(void)
{
    gfx_clear(COLOR_BG);
    gfx_text_centered(10, "TILT MAZE", COLOR_ACCENT, COLOR_BG, 2);
    gfx_text_centered(34, "TILT TO ROLL THE BALL", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(44, "INTO THE GREEN HOLE", COLOR_GOAL, COLOR_BG, 1);
    gfx_text_centered(54, "AVOID THE RED ONES", COLOR_TRAP, COLOR_BG, 1);
    gfx_text_centered(70, "B: SET FLAT  START: PAUSE", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(80, "SELECT: SOUND ON/OFF", COLOR_TEXT, COLOR_BG, 1);
    if (!_have_accel) {
        gfx_text_centered(96, "NO ACCELEROMETER!", COLOR_TRAP, COLOR_BG, 1);
    }
    gfx_text_centered(112, "HOLD FLAT, PRESS A", COLOR_ACCENT, COLOR_BG, 1);
    _wait_press(BTN_A);
    _calibrate();
}

int main(void)
{
    if (gfx_init() != 0) {
        puts("No display found");
        return 1;
    }
    buttons_init();
    tone_init(board_speaker_init());
    _have_accel = _accel_init();

    _title_screen();
    random_init(ztimer_now(ZTIMER_USEC));

    while (1) {
        uint32_t ms = _play_level();
        char buf[24];

        gfx_fill_rect(20, 40, gfx_width() - 40, 48, COLOR_BG);
        snprintf(buf, sizeof(buf), "LEVEL %u DONE", _level);
        gfx_text_centered(46, buf, COLOR_GOAL, COLOR_BG, 1);
        snprintf(buf, sizeof(buf), "%lu.%lu S", (unsigned long)(ms / 1000),
                 (unsigned long)((ms / 100) % 10));
        gfx_text_centered(58, buf, COLOR_TEXT, COLOR_BG, 2);
        gfx_text_centered(76, "PRESS A", COLOR_ACCENT, COLOR_BG, 1);
        _wait_press(BTN_A);
        _level++;
    }

    return 0;
}
