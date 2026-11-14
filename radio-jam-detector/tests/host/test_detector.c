/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the interference detector with synthetic scans
 *
 * The usual radio environment: Wi-Fi traffic on Wi-Fi channel 1 (channels
 * 11 to 14 of IEEE 802.15.4) busy 5 to 35% of the time, an IEEE 802.15.4
 * network on channel 26 busy up to 15% of the time, the other channels
 * mostly clear. Each scan samples each channel 24 times, like the board.
 * After learning 240 scans, the test checks that usual scans are rarely
 * flagged, and that interferences are flagged and described.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detector.h"

#define SAMPLES     (24)
#define TESTS       (400)

static float _rand(void)
{
    return (float)rand() / (float)RAND_MAX;
}

/* busy probabilities of the usual environment */
static void _usual(float p[DET_CHANNELS])
{
    float wifi = 0.05f + 0.3f * _rand();
    for (unsigned c = 0; c < DET_CHANNELS; c++) {
        p[c] = 0.01f;
    }
    for (unsigned c = 0; c < 4; c++) {
        p[c] = wifi * (c == 0 || c == 3 ? 0.6f : 1.0f);
    }
    p[15] = 0.15f * _rand();
}

static void _scan(const float p[DET_CHANNELS], float occupancy[DET_CHANNELS])
{
    for (unsigned c = 0; c < DET_CHANNELS; c++) {
        unsigned busy = 0;
        for (unsigned s = 0; s < SAMPLES; s++) {
            busy += _rand() < p[c];
        }
        occupancy[c] = (float)busy / SAMPLES;
    }
}

typedef struct {
    const char *name;
    unsigned first, last;       /* channels, 0 for none */
    float busy;
    det_kind_t kind;
} fault_t;

static const fault_t _faults[] = {
    { "jammer on channel 20, 50%", 20, 20, 0.5f, DET_NARROWBAND },
    { "jammer on channel 15, always on", 15, 15, 1.0f, DET_NARROWBAND },
    { "Wi-Fi channel 6 busy 60%", 16, 19, 0.6f, DET_WIDEBAND },
    { "Wi-Fi channel 11 busy 40%", 21, 24, 0.4f, DET_WIDEBAND },
};

int main(void)
{
    float p[DET_CHANNELS], occ[DET_CHANNELS];
    det_result_t r;
    int failed = 0;

    srand(7);
    det_learn_reset();
    for (unsigned i = 0; i < DET_TRAIN_MAX; i++) {
        _usual(p);
        _scan(p, occ);
        det_learn_add(occ);
    }
    if (det_learn_fit() != 0 || !det_trained()) {
        puts("FAIL: fit");
        return 1;
    }

    unsigned false_alarms = 0;
    for (unsigned t = 0; t < TESTS; t++) {
        _usual(p);
        _scan(p, occ);
        det_score(occ, &r);
        false_alarms += r.level >= DET_ALARM_LEVEL;
    }
    printf("usual scans: %u of %u flagged\n", false_alarms, TESTS);
    if (false_alarms > TESTS * 3 / 100) {
        failed = 1;
    }

    for (unsigned f = 0; f < sizeof(_faults) / sizeof(_faults[0]); f++) {
        const fault_t *fault = &_faults[f];
        unsigned flagged = 0, described = 0;
        for (unsigned t = 0; t < TESTS; t++) {
            _usual(p);
            for (unsigned c = fault->first; c <= fault->last; c++) {
                unsigned i = c - DET_FIRST_CHANNEL;
                p[i] = p[i] + fault->busy * (1.0f - p[i]);
            }
            _scan(p, occ);
            det_score(occ, &r);
            if (r.level >= DET_ALARM_LEVEL) {
                flagged++;
                if (r.kind == fault->kind && r.first == fault->first &&
                    r.last == fault->last) {
                    described++;
                }
            }
        }
        printf("%s: %u of %u flagged, %u described right\n", fault->name, flagged,
               TESTS, described);
        if (flagged < TESTS * 95 / 100 || described < TESTS * 80 / 100) {
            failed = 1;
        }
    }

    puts(failed ? "FAIL" : "PASS");
    return failed;
}
