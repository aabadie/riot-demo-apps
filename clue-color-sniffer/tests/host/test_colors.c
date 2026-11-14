/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the color recognition: teach six colors with
 *              noisy readings at varying distances, then recognize new
 *              readings of them and reject colors that were not taught
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "colors.h"

typedef struct {
    const char *name;
    float r, g, b;      /* reflectance */
} color_t;

static const color_t _taught[COLORS_SLOTS] = {
    { "red", 0.80f, 0.15f, 0.12f }, { "orange", 0.85f, 0.45f, 0.10f },
    { "yellow", 0.85f, 0.80f, 0.15f }, { "green", 0.15f, 0.60f, 0.20f },
    { "blue", 0.12f, 0.25f, 0.75f }, { "brown", 0.40f, 0.25f, 0.15f },
};

static const color_t _unknown[] = {
    { "purple", 0.55f, 0.15f, 0.65f }, { "cyan", 0.15f, 0.70f, 0.75f },
    { "white", 0.85f, 0.85f, 0.85f },
};

static float _rand(float spread)
{
    return ((float)rand() / RAND_MAX * 2 - 1) * spread;
}

static void _read(const color_t *c, float features[COLORS_FEATURES])
{
    /* white LED light, sensor channels a bit unbalanced, the object moves
       by +-30% of distance, 3% noise per channel */
    float light = 2000 * (1 + _rand(0.3f));
    float r = c->r * light * 1.0f * (1 + _rand(0.03f));
    float g = c->g * light * 1.2f * (1 + _rand(0.03f));
    float b = c->b * light * 0.9f * (1 + _rand(0.03f));
    float clear = (r + g + b) * 1.1f;

    colors_features((uint16_t)r, (uint16_t)g, (uint16_t)b, (uint16_t)clear,
                    features);
}

int main(void)
{
    float f[COLORS_FEATURES];
    unsigned correct = 0, total = 0, accepted = 0, unknown_total = 0;

    srand(3);
    colors_reset();
    for (unsigned s = 0; s < COLORS_SLOTS; s++) {
        for (int i = 0; i < 15; i++) {
            _read(&_taught[s], f);
            colors_teach(s, f);
        }
    }
    for (unsigned s = 0; s < COLORS_SLOTS; s++) {
        unsigned ok = 0;
        for (int i = 0; i < 100; i++) {
            _read(&_taught[s], f);
            ok += colors_predict(f, NULL) == (int)s;
        }
        printf("%-8s recognized %3u/100\n", _taught[s].name, ok);
        correct += ok;
        total += 100;
    }
    for (unsigned u = 0; u < sizeof(_unknown) / sizeof(_unknown[0]); u++) {
        unsigned acc = 0;
        for (int i = 0; i < 100; i++) {
            _read(&_unknown[u], f);
            acc += colors_predict(f, NULL) >= 0;
        }
        printf("%-8s (not taught) accepted %3u/100\n", _unknown[u].name, acc);
        accepted += acc;
        unknown_total += 100;
    }
    bool pass = correct >= total * 95 / 100 && accepted <= unknown_total / 20;
    printf("%u/%u recognized, %u/%u unknown readings accepted\n", correct,
           total, accepted, unknown_total);
    puts(pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
