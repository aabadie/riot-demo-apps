/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Pong rules and the scripted player
 */

#include <math.h>

#include "pong.h"

#define MAX_VX      (6.0f)
#define SERVE_VX    (1.8f)

static int _last_hit;

static uint32_t _rand(pong_t *g)
{
    /* xorshift32, same sequence on the board and in train.py */
    uint32_t x = g->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static float _uniform(pong_t *g, float lo, float hi)
{
    return lo + (hi - lo) * (float)(_rand(g) % 10000) / 10000.0f;
}

void pong_serve(pong_t *g, int towards)
{
    g->bx = (PONG_W - PONG_BALL) / 2.0f;
    g->by = _uniform(g, PONG_H * 0.2f, PONG_H * 0.8f);
    g->vx = towards * SERVE_VX;
    g->vy = _uniform(g, -1.2f, 1.2f);
    g->hits = 0;
}

void pong_init(pong_t *g, uint32_t seed, float bot_speed)
{
    g->rng = seed ? seed : 1;
    g->left = g->right = (PONG_H - PONG_PADDLE_H) / 2.0f;
    g->bot_speed = bot_speed;
    pong_serve(g, (_rand(g) & 1) ? 1 : -1);
}

static float _move(float y, int dir, float speed)
{
    y += dir * speed;
    if (y < 0) {
        y = 0;
    }
    if (y > PONG_H - PONG_PADDLE_H) {
        y = PONG_H - PONG_PADDLE_H;
    }
    return y;
}

static void _bounce(pong_t *g, float paddle, int side)
{
    /* the further from the center of the paddle, the steeper the bounce */
    float offset = (g->by + PONG_BALL / 2.0f - (paddle + PONG_PADDLE_H / 2.0f)) /
                   (PONG_PADDLE_H / 2.0f);
    float speed = fabsf(g->vx) * 1.08f;

    speed = speed > MAX_VX ? MAX_VX : speed;
    g->vx = -side * speed;
    g->vy = offset * 3.0f + g->vy * 0.3f;
    g->hits++;
    _last_hit = 1;
}

int pong_step(pong_t *g, int left, int right)
{
    _last_hit = 0;
    g->left = _move(g->left, left, PONG_SPEED);
    g->right = _move(g->right, right, g->bot_speed);

    g->bx += g->vx;
    g->by += g->vy;
    if (g->by < 0) {
        g->by = -g->by;
        g->vy = -g->vy;
    }
    if (g->by > PONG_H - PONG_BALL) {
        g->by = 2 * (PONG_H - PONG_BALL) - g->by;
        g->vy = -g->vy;
    }

    const float lx = PONG_PADDLE_X + PONG_PADDLE_W;
    const float rx = PONG_W - PONG_PADDLE_X - PONG_PADDLE_W;
    if (g->vx < 0 && g->bx <= lx && g->bx > lx - 2 * MAX_VX &&
        g->by + PONG_BALL >= g->left && g->by <= g->left + PONG_PADDLE_H) {
        g->bx = lx;
        _bounce(g, g->left, -1);
    }
    if (g->vx > 0 && g->bx + PONG_BALL >= rx && g->bx + PONG_BALL < rx + 2 * MAX_VX &&
        g->by + PONG_BALL >= g->right && g->by <= g->right + PONG_PADDLE_H) {
        g->bx = rx - PONG_BALL;
        _bounce(g, g->right, 1);
    }

    if (g->bx + PONG_BALL < 0) {
        return -1;
    }
    if (g->bx > PONG_W) {
        return 1;
    }
    return 0;
}

int pong_hit(const pong_t *g)
{
    (void)g;
    return _last_hit;
}

void pong_features(const pong_t *g, float f[PONG_FEATURES])
{
    f[0] = g->bx / PONG_W;
    f[1] = g->by / PONG_H;
    f[2] = g->vx / MAX_VX;
    f[3] = g->vy / 3.0f;
    f[4] = g->right / PONG_H;
    f[5] = (g->by + PONG_BALL / 2.0f - g->right - PONG_PADDLE_H / 2.0f) / PONG_H;
    f[6] = g->bot_speed / PONG_SPEED;
}

static float _arrival(const pong_t *g, float x)
{
    /* where the center of the ball will be when it reaches x */
    float t = (x - g->bx) / g->vx;
    float y = g->by + PONG_BALL / 2.0f + g->vy * t;
    float span = PONG_H - PONG_BALL;

    /* unfold the bounces on the walls */
    y -= PONG_BALL / 2.0f;
    y = fmodf(y, 2 * span);
    if (y < 0) {
        y += 2 * span;
    }
    if (y > span) {
        y = 2 * span - y;
    }
    return y + PONG_BALL / 2.0f;
}

int pong_expert(const pong_t *g, int side)
{
    float paddle = side < 0 ? g->left : g->right;
    float center = paddle + PONG_PADDLE_H / 2.0f;
    float target;
    float speed = side < 0 ? PONG_SPEED : g->bot_speed;

    if ((side > 0 && g->vx > 0) || (side < 0 && g->vx < 0)) {
        float x = side > 0 ? PONG_W - PONG_PADDLE_X - PONG_PADDLE_W - PONG_BALL
                           : PONG_PADDLE_X + PONG_PADDLE_W;
        target = _arrival(g, x);
    }
    else {
        /* wait in the middle, following the ball a bit */
        target = PONG_H / 2.0f + (g->by - PONG_H / 2.0f) * 0.3f;
    }
    if (target < center - speed) {
        return PONG_UP;
    }
    if (target > center + speed) {
        return PONG_DOWN;
    }
    return PONG_STAY;
}
