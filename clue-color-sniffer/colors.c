/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Colors taught on the device, Gaussian naive Bayes of emlearn
 *
 * Each slot keeps the running mean and variance of its samples (Welford's
 * algorithm). The emlearn model is rebuilt from them after each sample,
 * with the taught slots only.
 */

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "colors.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "eml_bayes.h"
#pragma GCC diagnostic pop

typedef struct {
    unsigned n;
    float mean[COLORS_FEATURES];
    float m2[COLORS_FEATURES];
} slot_t;

/* smallest standard deviation per feature, the sensor noise and the
   changes of the light make tighter models too picky */
static const float _std_min[COLORS_FEATURES] = { 0.008f, 0.008f, 0.08f };

static slot_t _slots[COLORS_SLOTS];
static EmlBayesSummary _summaries[COLORS_SLOTS * COLORS_FEATURES];
static EmlBayesModel _model = { 0, COLORS_FEATURES, _summaries };
static int _class_slot[COLORS_SLOTS];   /* model class to slot */

void colors_features(uint16_t red, uint16_t green, uint16_t blue,
                     uint16_t clear, float features[COLORS_FEATURES])
{
    float sum = (float)red + green + blue + 1.0f;

    features[0] = red / sum;
    features[1] = green / sum;
    features[2] = log10f(clear + 1.0f) / 4.0f;
}

static float _std(const slot_t *s, unsigned f)
{
    float var = s->n > 1 ? s->m2[f] / (s->n - 1) : 0;
    float std = sqrtf(var);

    return std < _std_min[f] ? _std_min[f] : std;
}

static void _rebuild(void)
{
    int classes = 0;

    for (unsigned s = 0; s < COLORS_SLOTS; s++) {
        if (_slots[s].n < COLORS_MIN_SAMPLES) {
            continue;
        }
        for (unsigned f = 0; f < COLORS_FEATURES; f++) {
            float std = _std(&_slots[s], f);
            EmlBayesSummary *sum = &_summaries[classes * COLORS_FEATURES + f];
            sum->mean = EML_Q16_FROMFLOAT(_slots[s].mean[f]);
            sum->std = EML_Q16_FROMFLOAT(std);
            sum->stdlog2 = EML_Q16_FROMFLOAT(log2f(std));
        }
        _class_slot[classes++] = s;
    }
    _model.n_classes = classes;
}

void colors_reset(void)
{
    memset(_slots, 0, sizeof(_slots));
    _rebuild();
}

void colors_clear(unsigned slot)
{
    if (slot < COLORS_SLOTS) {
        memset(&_slots[slot], 0, sizeof(_slots[slot]));
        _rebuild();
    }
}

void colors_teach(unsigned slot, const float features[COLORS_FEATURES])
{
    if (slot >= COLORS_SLOTS) {
        return;
    }
    slot_t *s = &_slots[slot];
    s->n++;
    for (unsigned f = 0; f < COLORS_FEATURES; f++) {
        float d = features[f] - s->mean[f];
        s->mean[f] += d / s->n;
        s->m2[f] += d * (features[f] - s->mean[f]);
    }
    _rebuild();
}

unsigned colors_count(unsigned slot)
{
    return slot < COLORS_SLOTS ? _slots[slot].n : 0;
}

static float _z(const slot_t *s, const float features[COLORS_FEATURES])
{
    float z = 0;

    for (unsigned f = 0; f < COLORS_FEATURES; f++) {
        z += fabsf(features[f] - s->mean[f]) / _std(s, f);
    }
    return z / COLORS_FEATURES;
}

int colors_predict(const float features[COLORS_FEATURES], float *z)
{
    int slot = -1;

    if (_model.n_classes >= 2) {
        int32_t c = eml_bayes_predict(&_model, features, COLORS_FEATURES);
        slot = c >= 0 ? _class_slot[c] : -1;
    }
    else if (_model.n_classes == 1) {
        /* emlearn needs two classes, one slot is only a distance check */
        slot = _class_slot[0];
    }
    if (slot < 0) {
        return -1;
    }
    float dist = _z(&_slots[slot], features);
    if (z) {
        *z = dist;
    }
    return dist <= COLORS_MAX_Z ? slot : -1;
}
