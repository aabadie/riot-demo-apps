/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Gesture features
 *
 * For each channel (6 sensor channels and the dynamic acceleration
 * |a| - 1 g): mean, standard deviation, minimum, maximum, the means of the
 * 4 quarters of the gesture, the mean absolute change between two samples,
 * and where the maximum is compared to the minimum. Plus the duration. The
 * acceleration is in milli-g, the rotation in tenths of degrees per second.
 */

#include <math.h>

#include "gesture.h"

static int16_t _clamp(float v)
{
    v = roundf(v);
    if (v > INT16_MAX) {
        return INT16_MAX;
    }
    if (v < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)v;
}

static float _value(const float *samples, unsigned i, unsigned ch)
{
    const float *s = &samples[i * GESTURE_CHANNELS];

    if (ch < GESTURE_CHANNELS) {
        return s[ch];
    }
    return sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]) - 1.0f;
}

int gesture_features(const float *samples, unsigned n, int16_t *features)
{
    if (n < GESTURE_MIN_SAMPLES) {
        return -1;
    }

    int16_t *out = features;
    for (unsigned ch = 0; ch < GESTURE_FEAT_CHANNELS; ch++) {
        /* gyroscope channels in tenths of dps, others in milli-g */
        const float scale = (ch >= 3 && ch < 6) ? 10.0f : 1000.0f;
        float sum = 0, sum2 = 0, min = INFINITY, max = -INFINITY, change = 0;
        float quarter[4] = { 0 };
        unsigned count[4] = { 0 };
        unsigned i_min = 0, i_max = 0;

        for (unsigned i = 0; i < n; i++) {
            float v = _value(samples, i, ch);
            sum += v;
            sum2 += v * v;
            if (v < min) {
                min = v;
                i_min = i;
            }
            if (v > max) {
                max = v;
                i_max = i;
            }
            unsigned q = i * 4 / n;
            quarter[q] += v;
            count[q]++;
            if (i > 0) {
                change += fabsf(v - _value(samples, i - 1, ch));
            }
        }
        float mean = sum / n;
        float var = sum2 / n - mean * mean;

        *out++ = _clamp(mean * scale);
        *out++ = _clamp(sqrtf(var > 0 ? var : 0) * scale);
        *out++ = _clamp(min * scale);
        *out++ = _clamp(max * scale);
        for (unsigned q = 0; q < 4; q++) {
            *out++ = _clamp(count[q] ? quarter[q] / count[q] * scale : 0);
        }
        *out++ = _clamp(change / (n - 1) * scale);
        *out++ = _clamp(((float)i_max - (float)i_min) * 1000.0f / n);
    }
    /* duration in milliseconds */
    *out++ = _clamp(n * 1000.0f / GESTURE_RATE_HZ);
    return 0;
}
