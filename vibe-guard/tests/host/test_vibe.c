/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the vibration anomaly detection on synthetic
 *              signals: a machine running at 30 Hz, learned as normal, then
 *              the same machine (no alarm expected) and faulty variants
 *              (alarm expected)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vibe.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    const char *name;
    float amp_30;       /* main vibration, g */
    float amp_60;       /* harmonic */
    float amp_new;      /* new component, at new_hz */
    float new_hz;
    float knocks;       /* knocks per second */
    bool anomaly;
} scenario_t;

static const scenario_t _normal = { "normal", 0.05f, 0.01f, 0, 0, 0, false };

static const scenario_t _tests[] = {
    { "normal again", 0.05f, 0.01f, 0, 0, 0, false },
    { "slightly louder (+20%)", 0.06f, 0.012f, 0, 0, 0, false },
    { "three times stronger", 0.15f, 0.03f, 0, 0, 0, true },
    { "bearing whine at 85 Hz", 0.05f, 0.01f, 0.02f, 85, 0, true },
    { "unbalance at 8 Hz", 0.05f, 0.01f, 0.04f, 8, 0, true },
    { "knocking", 0.05f, 0.01f, 0, 0, 3, true },
    { "stopped", 0, 0, 0, 0, 0, true },
};

static float _noise(void)
{
    /* roughly gaussian, unit variance */
    float s = 0;

    for (int i = 0; i < 12; i++) {
        s += (float)rand() / RAND_MAX;
    }
    return s - 6.0f;
}

static unsigned long _t;
static double _phase;   /* of the main vibration, accumulated */

static void _window(const scenario_t *sc, float w[VIBE_WINDOW][3])
{
    /* keep the second half of the previous window, like the board */
    static float buf[VIBE_WINDOW][3];

    for (unsigned i = 0; i < VIBE_WINDOW - VIBE_HOP; i++) {
        for (int a = 0; a < 3; a++) {
            buf[i][a] = buf[i + VIBE_HOP][a];
        }
    }
    for (unsigned i = VIBE_WINDOW - VIBE_HOP; i < VIBE_WINDOW; i++, _t++) {
        float t = (float)_t / VIBE_RATE_HZ;
        /* speed wanders a little around 30 Hz */
        float f = 30.0f + 0.3f * sinf(2 * (float)M_PI * 0.1f * t);
        _phase = fmod(_phase + 2 * M_PI * f / VIBE_RATE_HZ, 2 * M_PI);
        float v = sc->amp_30 * sinf((float)_phase) +
                  sc->amp_60 * sinf(2 * (float)_phase + 0.5f) +
                  sc->amp_new * sinf(2 * (float)M_PI * sc->new_hz * t);
        if (sc->knocks > 0) {
            float period = 1.0f / sc->knocks;
            float phase = fmodf(t, period);
            v += 0.4f * expf(-phase * 60.0f) * sinf(2 * (float)M_PI * 45 * phase);
        }
        buf[i][0] = v + 0.004f * _noise();
        buf[i][1] = 0.5f * v + 0.004f * _noise();
        buf[i][2] = 1.0f + 0.2f * v + 0.004f * _noise();
    }
    for (unsigned i = 0; i < VIBE_WINDOW; i++) {
        for (int a = 0; a < 3; a++) {
            w[i][a] = buf[i][a];
        }
    }
}

int main(void)
{
    static float w[VIBE_WINDOW][3];
    float feat[VIBE_BANDS];
    int failures = 0;

    srand(1);
    vibe_init();
    vibe_learn_reset();

    /* 2 windows to fill the buffer, then 30 s of learning */
    _window(&_normal, w);
    _window(&_normal, w);
    unsigned windows = 30 * VIBE_RATE_HZ / VIBE_HOP;
    for (unsigned i = 0; i < windows; i++) {
        _window(&_normal, w);
        vibe_features(w, feat);
        vibe_learn_add(feat);
    }
    if (vibe_learn_fit() != 0) {
        puts("FAIL: fit");
        return 1;
    }
    printf("learned %u windows\n", windows);

    for (unsigned s = 0; s < sizeof(_tests) / sizeof(_tests[0]); s++) {
        const scenario_t *sc = &_tests[s];
        unsigned odd = 0, n = 100;
        float max = 0;
        /* let the window fill with the new signal first */
        _window(sc, w);
        _window(sc, w);
        for (unsigned i = 0; i < n; i++) {
            _window(sc, w);
            vibe_features(w, feat);
            float level = vibe_level(feat, NULL);
            max = level > max ? level : max;
            if (level >= VIBE_ALARM_LEVEL) {
                odd++;
            }
        }
        /* an alarm needs 3 odd windows in a row on the board, here a share
           of the windows is enough */
        bool alarm = odd >= n / 2;
        bool ok = alarm == sc->anomaly;
        failures += !ok;
        printf("%-26s odd windows %3u/%u, max level %6.1f: %s%s\n", sc->name,
               odd, n, max, alarm ? "alarm" : "quiet", ok ? "" : "  <- FAIL");
    }
    puts(failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
