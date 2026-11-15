/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Stackfall: a falling blocks puzzle for the PyBadge
 *
 * Controls: left/right move, down drops faster, up drops at once, A turns
 * clockwise, B turns counterclockwise, START pauses, SELECT toggles the
 * music.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "random.h"
#include "ztimer.h"

#include "demo_board.h"
#include "demo_gfx.h"
#include "demo_tone.h"

#define COLS            (10)
#define ROWS            (20)
#define CELL            (6)
#define TICK_MS         (10)

#define MOVE_DELAY_MS   (170)   /* before a held left/right repeats */
#define MOVE_REPEAT_MS  (50)
#define SOFT_DROP_MS    (35)
#define LOCK_MS         (450)
#define LOCK_RESETS     (10)

#define NUM_PIECES      (7)
#define GHOST           (0x80)

#define COLOR_BG        GFX_RGB(12, 14, 34)
#define COLOR_FIELD     GFX_RGB(4, 5, 16)
#define COLOR_FRAME     GFX_RGB(90, 100, 160)
#define COLOR_TEXT      GFX_RGB(220, 225, 255)
#define COLOR_LABEL     GFX_RGB(130, 140, 200)
#define COLOR_ACCENT    GFX_RGB(255, 200, 70)

typedef struct {
    uint8_t size;           /* rotation box size */
    int8_t cells[4][2];     /* x, y in the box, first rotation */
    uint8_t r, g, b;        /* base color */
} piece_def_t;

static const piece_def_t _pieces[NUM_PIECES] = {
    { 4, { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } }, 255, 110, 90 },   /* long bar, coral */
    { 3, { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, 90, 170, 255 },   /* hook, sky */
    { 3, { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, 255, 190, 60 },   /* other hook, amber */
    { 4, { { 1, 0 }, { 2, 0 }, { 1, 1 }, { 2, 1 } }, 150, 220, 70 },   /* square, lime */
    { 3, { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } }, 170, 110, 255 },  /* step, violet */
    { 3, { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, 255, 100, 180 },  /* tee, rose */
    { 3, { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } }, 80, 220, 170 },   /* other step, mint */
};

typedef struct {
    int type;
    int rot;
    int x;
    int y;
} piece_t;

/* 0 is empty, 1..7 is a piece type + 1, GHOST marks the drop preview */
static uint8_t _board[ROWS][COLS];
static uint8_t _shown[ROWS][COLS];
static uint16_t _cell_px[NUM_PIECES + 1][2][CELL * CELL];

static piece_t _cur;
static int _next;
static uint8_t _bag[NUM_PIECES];
static unsigned _bag_left;

static unsigned _score;
static unsigned _lines;
static unsigned _level;
static unsigned _best;
static bool _music_on = true;

static buttons_t _buttons;
static int _field_x;
static int _field_y;

static const uint16_t _gravity_ms[] = {
    800, 720, 630, 550, 470, 380, 300, 220, 160, 120, 100, 85, 70, 60, 50,
};

static const uint16_t _line_points[] = { 0, 100, 250, 450, 700 };

/* An original tune in A minor; each note is followed by a short gap */
#define E8      (170)
#define N(f, d) { f, (d) - 25 }, { NOTE_REST, 25 }

static const tone_note_t _music[] = {
    N(NOTE_A4, E8), N(NOTE_C5, E8), N(NOTE_E5, E8), N(NOTE_A5, E8),
    N(NOTE_G5, 2 * E8), N(NOTE_E5, E8), N(NOTE_D5, E8),
    N(NOTE_E5, 3 * E8), N(NOTE_C5, E8), N(NOTE_D5, 2 * E8), N(NOTE_B4, 2 * E8),
    N(NOTE_G4, E8), N(NOTE_B4, E8), N(NOTE_D5, E8), N(NOTE_G5, E8),
    N(NOTE_F5, 2 * E8), N(NOTE_D5, E8), N(NOTE_C5, E8),
    N(NOTE_D5, 3 * E8), N(NOTE_B4, E8), N(NOTE_C5, 4 * E8),
    N(NOTE_F5, E8), N(NOTE_E5, E8), N(NOTE_D5, E8), N(NOTE_C5, E8),
    N(NOTE_D5, 2 * E8), N(NOTE_A4, 2 * E8),
    N(NOTE_E5, E8), N(NOTE_D5, E8), N(NOTE_C5, E8), N(NOTE_B4, E8),
    N(NOTE_C5, 2 * E8), N(NOTE_G4, 2 * E8),
    N(NOTE_A4, E8), N(NOTE_C5, E8), N(NOTE_E5, E8), N(NOTE_G5, E8),
    N(NOTE_A5, 2 * E8), N(NOTE_G5, E8), N(NOTE_E5, E8),
    N(NOTE_D5, E8), N(NOTE_E5, E8), N(NOTE_C5, E8), N(NOTE_B4, E8),
    N(NOTE_A4, 4 * E8),
};

static const tone_note_t _sfx_clear[] = {
    { NOTE_C6, 50 }, { NOTE_E6, 50 }, { NOTE_G6, 80 },
};

static const tone_note_t _sfx_four[] = {
    { NOTE_C6, 60 }, { NOTE_E6, 60 }, { NOTE_G6, 60 }, { NOTE_C6, 60 },
    { NOTE_E6, 60 }, { NOTE_A6, 160 },
};

static const tone_note_t _sfx_level[] = {
    { NOTE_G5, 70 }, { NOTE_C6, 70 }, { NOTE_E6, 70 }, { NOTE_G6, 140 },
};

static const tone_note_t _sfx_over[] = {
    { NOTE_E5, 150 }, { NOTE_C5, 150 }, { NOTE_A4, 150 }, { NOTE_E4, 400 },
};

static uint16_t _scale(uint8_t r, uint8_t g, uint8_t b, int num, int den)
{
    int rr = r * num / den, gg = g * num / den, bb = b * num / den;
    return GFX_RGB(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb);
}

/* Precompute the bevelled 6x6 block of each color, and its ghost outline */
static void _build_cells(void)
{
    for (int t = 0; t <= NUM_PIECES; t++) {
        uint8_t r = 255, g = 255, b = 255;
        if (t < NUM_PIECES) {
            r = _pieces[t].r;
            g = _pieces[t].g;
            b = _pieces[t].b;
        }
        uint16_t light = _scale(r, g, b, 5, 4);
        uint16_t mid = _scale(r, g, b, 1, 1);
        uint16_t dark = _scale(r, g, b, 1, 2);
        uint16_t shine = GFX_RGB(255, 255, 255);
        uint16_t dim = _scale(r, g, b, 1, 3);

        for (int y = 0; y < CELL; y++) {
            for (int x = 0; x < CELL; x++) {
                uint16_t c = mid;
                if (y == 0 || x == 0) {
                    c = light;
                }
                if (y == CELL - 1 || x == CELL - 1) {
                    c = dark;
                }
                if (x == 1 && y == 1) {
                    c = shine;
                }
                _cell_px[t][0][y * CELL + x] = c;

                bool edge = (x == 0 || y == 0 || x == CELL - 1 || y == CELL - 1);
                _cell_px[t][1][y * CELL + x] = edge ? dim : COLOR_FIELD;
            }
        }
    }
}

static void _draw_cell(int px, int py, uint8_t value)
{
    if (value == 0) {
        gfx_fill_rect(px, py, CELL, CELL, COLOR_FIELD);
        return;
    }
    int ghost = (value & GHOST) ? 1 : 0;
    int type = (value & ~GHOST) - 1;
    gfx_pixels(px, py, CELL, CELL, _cell_px[type][ghost]);
}

static void _piece_cells(const piece_t *p, int out[4][2])
{
    const piece_def_t *def = &_pieces[p->type];
    for (int i = 0; i < 4; i++) {
        int x = def->cells[i][0];
        int y = def->cells[i][1];
        /* the square does not turn */
        int turns = (def->size == 4 && p->type == 3) ? 0 : p->rot;
        for (int r = 0; r < turns; r++) {
            int nx = def->size - 1 - y;
            y = x;
            x = nx;
        }
        out[i][0] = p->x + x;
        out[i][1] = p->y + y;
    }
}

static bool _fits(const piece_t *p)
{
    int cells[4][2];
    _piece_cells(p, cells);
    for (int i = 0; i < 4; i++) {
        int x = cells[i][0];
        int y = cells[i][1];
        if (x < 0 || x >= COLS || y >= ROWS) {
            return false;
        }
        if (y >= 0 && _board[y][x]) {
            return false;
        }
    }
    return true;
}

static int _drop_distance(const piece_t *p)
{
    piece_t test = *p;
    int d = 0;
    while (1) {
        test.y++;
        if (!_fits(&test)) {
            return d;
        }
        d++;
    }
}

/* Draw the cells that differ from what is on screen */
static void _render(void)
{
    uint8_t frame[ROWS][COLS];
    int cells[4][2];

    memcpy(frame, _board, sizeof(frame));

    piece_t ghost = _cur;
    ghost.y += _drop_distance(&_cur);
    _piece_cells(&ghost, cells);
    for (int i = 0; i < 4; i++) {
        if (cells[i][1] >= 0) {
            frame[cells[i][1]][cells[i][0]] = (uint8_t)(GHOST | (_cur.type + 1));
        }
    }
    _piece_cells(&_cur, cells);
    for (int i = 0; i < 4; i++) {
        if (cells[i][1] >= 0) {
            frame[cells[i][1]][cells[i][0]] = (uint8_t)(_cur.type + 1);
        }
    }

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (frame[y][x] != _shown[y][x]) {
                _draw_cell(_field_x + x * CELL, _field_y + y * CELL, frame[y][x]);
                _shown[y][x] = frame[y][x];
            }
        }
    }
}

static void _invalidate(void)
{
    memset(_shown, 0xff, sizeof(_shown));
}

static void _draw_number(int x, int y, const char *label, unsigned value)
{
    char buf[12];
    gfx_text(x, y, label, COLOR_LABEL, COLOR_BG, 1);
    snprintf(buf, sizeof(buf), "%-7u", value);
    gfx_text(x, y + 10, buf, COLOR_TEXT, COLOR_BG, 1);
}

static void _draw_stats(void)
{
    int x = _field_x + COLS * CELL + 6;
    _draw_number(x, 6, "SCORE", _score);
    _draw_number(x, 32, "LINES", _lines);
    _draw_number(x, 58, "LEVEL", _level);
    _draw_number(x, 84, "BEST", _best);
}

static void _draw_next(void)
{
    const int box_x = 8;
    const int box_y = 22;
    piece_t p = { .type = _next, .rot = 0, .x = 0, .y = 0 };
    int cells[4][2];

    gfx_fill_rect(box_x, box_y, 4 * CELL + 4, 3 * CELL + 4, COLOR_FIELD);
    _piece_cells(&p, cells);
    /* center the 3 wide pieces in the 4 wide box */
    int offset = (_pieces[_next].size == 3) ? CELL / 2 : 0;
    for (int i = 0; i < 4; i++) {
        _draw_cell(box_x + 2 + offset + cells[i][0] * CELL, box_y + 2 + cells[i][1] * CELL,
                   (uint8_t)(_next + 1));
    }
}

static void _draw_frame(void)
{
    gfx_clear(COLOR_BG);
    gfx_fill_rect(_field_x - 2, _field_y - 2, COLS * CELL + 4, ROWS * CELL + 4, COLOR_FRAME);
    gfx_fill_rect(_field_x, _field_y, COLS * CELL, ROWS * CELL, COLOR_FIELD);
    gfx_text(8, 8, "NEXT", COLOR_LABEL, COLOR_BG, 1);
    gfx_text(4, 70, "STACK", COLOR_ACCENT, COLOR_BG, 1);
    gfx_text(10, 80, "FALL", COLOR_ACCENT, COLOR_BG, 1);
    gfx_text(2, 104, "SEL:", COLOR_LABEL, COLOR_BG, 1);
    gfx_text(2, 114, "MUSIC", COLOR_LABEL, COLOR_BG, 1);
    _invalidate();
}

static int _draw_from_bag(void)
{
    if (_bag_left == 0) {
        for (int i = 0; i < NUM_PIECES; i++) {
            _bag[i] = (uint8_t)i;
        }
        _bag_left = NUM_PIECES;
    }
    unsigned idx = random_uint32_range(0, _bag_left);
    int type = _bag[idx];
    _bag[idx] = _bag[--_bag_left];
    return type;
}

/* Spawn the next piece, return false when it does not fit: game over */
static bool _spawn(void)
{
    _cur.type = _next;
    _cur.rot = 0;
    _cur.x = (COLS - _pieces[_cur.type].size) / 2;
    _cur.y = -1;
    _next = _draw_from_bag();
    _draw_next();
    return _fits(&_cur);
}

static bool _try_move(int dx, int dy)
{
    piece_t test = _cur;
    test.x += dx;
    test.y += dy;
    if (_fits(&test)) {
        _cur = test;
        return true;
    }
    return false;
}

static bool _try_rotate(int dir)
{
    static const int8_t kicks[][2] = {
        { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { -2, 0 }, { 2, 0 },
    };
    for (unsigned i = 0; i < ARRAY_SIZE(kicks); i++) {
        piece_t test = _cur;
        test.rot = (test.rot + dir + 4) % 4;
        test.x += kicks[i][0];
        test.y += kicks[i][1];
        if (_fits(&test)) {
            _cur = test;
            tone_beep(NOTE_E6, 8);
            return true;
        }
    }
    return false;
}

static void _flash_rows(const int *rows, int count)
{
    for (int f = 0; f < 3; f++) {
        for (int i = 0; i < count; i++) {
            for (int x = 0; x < COLS; x++) {
                _draw_cell(_field_x + x * CELL, _field_y + rows[i] * CELL,
                           (f % 2) ? _board[rows[i]][x] : (uint8_t)NUM_PIECES + 1);
            }
        }
        ztimer_sleep(ZTIMER_MSEC, 60);
    }
}

/* Place the piece, clear the full rows and update the score. Return false
   when a part of the piece stays above the board: game over */
static bool _lock(void)
{
    int cells[4][2];
    int full[4];
    int count = 0;
    bool inside = true;

    _piece_cells(&_cur, cells);
    for (int i = 0; i < 4; i++) {
        if (cells[i][1] >= 0) {
            _board[cells[i][1]][cells[i][0]] = (uint8_t)(_cur.type + 1);
        }
        else {
            inside = false;
        }
    }

    for (int y = 0; y < ROWS; y++) {
        bool is_full = true;
        for (int x = 0; x < COLS; x++) {
            if (!_board[y][x]) {
                is_full = false;
                break;
            }
        }
        if (is_full && count < 4) {
            full[count++] = y;
        }
    }

    if (count == 0) {
        tone_beep(NOTE_C4, 25);
        return inside;
    }

    tone_effect(count == 4 ? _sfx_four : _sfx_clear,
                count == 4 ? ARRAY_SIZE(_sfx_four) : ARRAY_SIZE(_sfx_clear));
    _flash_rows(full, count);

    for (int i = 0; i < count; i++) {
        for (int y = full[i]; y > 0; y--) {
            memcpy(_board[y], _board[y - 1], COLS);
        }
        memset(_board[0], 0, COLS);
    }

    _score += _line_points[count] * _level;
    unsigned old_level = _level;
    _lines += count;
    _level = 1 + _lines / 10;
    if (_level > ARRAY_SIZE(_gravity_ms)) {
        _level = ARRAY_SIZE(_gravity_ms);
    }
    if (_level != old_level) {
        tone_effect(_sfx_level, ARRAY_SIZE(_sfx_level));
    }
    _draw_stats();
    return inside;
}

static void _wait_start(void)
{
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (_buttons.held);
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (!(_buttons.pressed & (BTN_START | BTN_A)));
}

static void _title_screen(void)
{
    gfx_clear(COLOR_BG);
    /* a few blocks as a logo */
    static const uint8_t logo[][2] = {
        { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 }, { 4, 1 }, { 5, 1 }, { 5, 0 }, { 6, 0 },
        { 8, 0 }, { 8, 1 }, { 9, 1 }, { 10, 1 },
    };
    for (unsigned i = 0; i < ARRAY_SIZE(logo); i++) {
        int x = (gfx_width() - 11 * CELL * 2) / 2 + logo[i][0] * CELL * 2;
        _draw_cell(x, 14 + logo[i][1] * CELL, (uint8_t)(1 + (i % NUM_PIECES)));
        _draw_cell(x + CELL, 14 + logo[i][1] * CELL, (uint8_t)(1 + (i % NUM_PIECES)));
    }
    gfx_text_centered(34, "STACKFALL", COLOR_ACCENT, COLOR_BG, 2);
    gfx_text_centered(56, "LEFT/RIGHT: MOVE", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(66, "DOWN: FAST  UP: DROP", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(76, "A/B: TURN  START: PAUSE", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(86, "SELECT: MUSIC ON/OFF", COLOR_TEXT, COLOR_BG, 1);
    gfx_text_centered(106, "PRESS START", COLOR_ACCENT, COLOR_BG, 1);
    _wait_start();
}

static void _pause(void)
{
    tone_stop();
    gfx_fill_rect(_field_x, _field_y + 50, COLS * CELL, 20, COLOR_BG);
    gfx_text(_field_x + (COLS * CELL - gfx_text_width("PAUSE", 1)) / 2, _field_y + 56,
             "PAUSE", COLOR_ACCENT, COLOR_BG, 1);
    do {
        buttons_update(&_buttons);
        ztimer_sleep(ZTIMER_MSEC, 20);
    } while (!(_buttons.pressed & BTN_START));
    gfx_fill_rect(_field_x, _field_y + 50, COLS * CELL, 20, COLOR_FIELD);
    _invalidate();
    if (_music_on) {
        tone_play(_music, ARRAY_SIZE(_music), true);
    }
}

static void _game_over(void)
{
    tone_stop();
    tone_effect(_sfx_over, ARRAY_SIZE(_sfx_over));

    /* fill the field from the bottom */
    for (int y = ROWS - 1; y >= 0; y--) {
        for (int x = 0; x < COLS; x++) {
            _draw_cell(_field_x + x * CELL, _field_y + y * CELL, (uint8_t)(1 + (y % NUM_PIECES)));
        }
        ztimer_sleep(ZTIMER_MSEC, 25);
    }
    if (_score > _best) {
        _best = _score;
    }
    gfx_fill_rect(_field_x - 2, _field_y + 40, COLS * CELL + 4, 36, COLOR_BG);
    gfx_text(_field_x + 3, _field_y + 46, "GAME", COLOR_ACCENT, COLOR_BG, 2);
    gfx_text(_field_x + 3, _field_y + 62, "OVER", COLOR_ACCENT, COLOR_BG, 2);
    _draw_stats();
    ztimer_sleep(ZTIMER_MSEC, 800);
    _wait_start();
}

static void _play(void)
{
    memset(_board, 0, sizeof(_board));
    _score = 0;
    _lines = 0;
    _level = 1;
    _bag_left = 0;
    _next = _draw_from_bag();

    _draw_frame();
    _draw_stats();
    if (!_spawn()) {
        return;
    }
    if (_music_on) {
        tone_play(_music, ARRAY_SIZE(_music), true);
    }

    int gravity = 0;
    int lock = 0;
    int lock_resets = 0;
    int move_timer = 0;
    int last_dir = 0;
    int drop_timer = 0;
    uint32_t last = ztimer_now(ZTIMER_MSEC);

    while (1) {
        buttons_update(&_buttons);
        bool moved = false;

        if (_buttons.pressed & BTN_START) {
            _pause();
            last = ztimer_now(ZTIMER_MSEC);
            continue;
        }
        if (_buttons.pressed & BTN_SELECT) {
            _music_on = !_music_on;
            if (_music_on) {
                tone_play(_music, ARRAY_SIZE(_music), true);
            }
            else {
                tone_stop();
            }
        }

        /* left and right, with auto repeat */
        /* both held: the last one pressed */
        if (_buttons.pressed & BTN_LEFT) {
            last_dir = -1;
        }
        if (_buttons.pressed & BTN_RIGHT) {
            last_dir = 1;
        }
        int dir = 0;
        if ((_buttons.held & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) {
            dir = last_dir;
        }
        else if (_buttons.held & BTN_LEFT) {
            dir = -1;
        }
        else if (_buttons.held & BTN_RIGHT) {
            dir = 1;
        }
        if (_buttons.pressed & (BTN_LEFT | BTN_RIGHT)) {
            moved |= _try_move(dir, 0);
            move_timer = MOVE_DELAY_MS;
        }
        else if (dir) {
            move_timer -= TICK_MS;
            if (move_timer <= 0) {
                moved |= _try_move(dir, 0);
                move_timer = MOVE_REPEAT_MS;
            }
        }

        if (_buttons.pressed & BTN_A) {
            moved |= _try_rotate(1);
        }
        if (_buttons.pressed & BTN_B) {
            moved |= _try_rotate(-1);
        }

        if (_buttons.pressed & BTN_UP) {
            int d = _drop_distance(&_cur);
            _cur.y += d;
            _score += 2 * d;
            lock = LOCK_MS;
        }

        if (_buttons.held & BTN_DOWN) {
            drop_timer -= TICK_MS;
            if (drop_timer <= 0) {
                if (_try_move(0, 1)) {
                    _score++;
                    gravity = 0;
                }
                drop_timer = SOFT_DROP_MS;
            }
        }
        else {
            drop_timer = 0;
        }

        gravity += TICK_MS;
        if (gravity >= _gravity_ms[_level - 1]) {
            gravity = 0;
            _try_move(0, 1);
        }

        /* lock once the piece rests for LOCK_MS, moving delays it a bit */
        if (_drop_distance(&_cur) == 0) {
            if (moved && lock_resets < LOCK_RESETS) {
                lock = 0;
                lock_resets++;
            }
            lock += TICK_MS;
            if (lock >= LOCK_MS) {
                bool inside = _lock();
                _invalidate();
                lock = 0;
                lock_resets = 0;
                gravity = 0;
                _draw_stats();
                if (!inside || !_spawn()) {
                    _render();
                    return;
                }
            }
        }
        else {
            lock = 0;
        }

        _render();
        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, TICK_MS);
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
    _build_cells();

    _field_x = (gfx_width() - COLS * CELL) / 2;
    _field_y = (gfx_height() - ROWS * CELL) / 2;

    while (1) {
        _title_screen();
        random_init(ztimer_now(ZTIMER_USEC));
        _play();
        _game_over();
    }

    return 0;
}
