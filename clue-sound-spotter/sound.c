/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Sound event detection and features, with the emlearn FFT
 */

#include <math.h>
#include <string.h>

#include "sound.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BINS        (SOUND_FRAME / 2)
#define BIN_HZ      ((float)SOUND_RATE / SOUND_FRAME)
#define POWER_MIN   (1e-3f)

static float _hann[SOUND_FRAME];
static unsigned _band_lo[SOUND_BANDS], _band_hi[SOUND_BANDS];

/* detector */
static unsigned _frame_no;
static bool _have_floor;
static float _floor;
static bool _in_event;
static unsigned _start, _last_loud, _quiet;
static unsigned _event_first, _event_len;

void sound_init(void)
{
    sound_fft_init();
    for (unsigned i = 0; i < SOUND_FRAME; i++) {
        _hann[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (SOUND_FRAME - 1));
    }
    /* log spaced bands from 100 Hz to 8 kHz, at least one bin each */
    unsigned lo = (unsigned)lroundf(100.0f / BIN_HZ);
    for (unsigned b = 0; b < SOUND_BANDS; b++) {
        float edge = 100.0f * powf(80.0f, (float)(b + 1) / SOUND_BANDS);
        unsigned hi = (unsigned)lroundf(edge / BIN_HZ);
        hi = hi <= lo ? lo + 1 : hi;
        hi = hi > BINS ? BINS : hi;
        _band_lo[b] = lo;
        _band_hi[b] = hi;
        lo = hi;
    }
    _frame_no = 0;
    _have_floor = false;
    _in_event = false;
    _event_len = 0;
}

void sound_analyze(const int16_t *pcm, sound_frame_t *out)
{
    static float re[SOUND_FRAME], im[SOUND_FRAME];
    float power[BINS];
    float mean = 0;

    for (unsigned i = 0; i < SOUND_FRAME; i++) {
        mean += pcm[i];
    }
    mean /= SOUND_FRAME;
    for (unsigned i = 0; i < SOUND_FRAME; i++) {
        re[i] = (pcm[i] - mean) * _hann[i] / 32768.0f;
        im[i] = 0;
    }
    sound_fft(re, im);

    float total = 0, weighted = 0, log_sum = 0, best = -1;
    unsigned best_bin = 0;
    for (unsigned i = 1; i < BINS; i++) {
        /* scaled so that a full scale sine is about 0 dB */
        power[i] = (re[i] * re[i] + im[i] * im[i]) * 16.0f / SOUND_FRAME + 1e-9f;
        total += power[i];
        weighted += power[i] * i * BIN_HZ;
        log_sum += logf(power[i]);
        if (power[i] > best) {
            best = power[i];
            best_bin = i;
        }
    }
    for (unsigned b = 0; b < SOUND_BANDS; b++) {
        float sum = 0;
        for (unsigned i = _band_lo[b]; i < _band_hi[b]; i++) {
            sum += power[i];
        }
        out->band[b] = 10.0f * log10f(sum + 1e-9f);
    }
    out->level = 10.0f * log10f(total + 1e-9f);
    out->centroid = weighted / total;
    out->flatness = expf(log_sum / (BINS - 1)) / (total / (BINS - 1));
    out->peak_hz = best_bin * BIN_HZ;
}

sound_event_t sound_detect(const sound_frame_t *frame)
{
    unsigned now = _frame_no++;
    float level = frame->level;

    if (!_have_floor) {
        _floor = level;
        _have_floor = true;
    }
    if (!_in_event) {
        if (level > _floor + SOUND_START_DB) {
            _in_event = true;
            _start = now;
            _last_loud = now;
            _quiet = 0;
            return SOUND_START;
        }
        /* falls fast, rises slowly, about 1 dB per second */
        if (level < _floor) {
            _floor = 0.8f * _floor + 0.2f * level;
        }
        else {
            _floor += 0.016f;
        }
        return SOUND_NONE;
    }

    if (level > _floor + SOUND_STOP_DB) {
        _last_loud = now;
        _quiet = 0;
    }
    else {
        _quiet++;
    }
    if (_quiet >= SOUND_STOP_FRAMES || now - _start + 1 >= SOUND_MAX_FRAMES) {
        _in_event = false;
        _event_first = _start;
        _event_len = _last_loud - _start + 1;
        return SOUND_END;
    }
    return SOUND_NONE;
}

unsigned sound_event(unsigned *first)
{
    if (first) {
        *first = _event_first;
    }
    return _event_len;
}

float sound_floor(void)
{
    return _floor;
}

static int16_t _q(float v)
{
    v = roundf(v);
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
}

int sound_features(const sound_frame_t *frames, unsigned n, int16_t *features)
{
    if (n < SOUND_MIN_FRAMES) {
        return -1;
    }

    unsigned peak = 0;
    for (unsigned i = 1; i < n; i++) {
        if (frames[i].level > frames[peak].level) {
            peak = i;
        }
    }
    const float ref = frames[peak].level;

    /* spectrum relative to the loudness of the event, in tenths of dB */
    for (unsigned b = 0; b < SOUND_BANDS; b++) {
        float sum = 0, max = -INFINITY;
        for (unsigned i = 0; i < n; i++) {
            float v = frames[i].band[b] - ref;
            sum += v;
            max = v > max ? v : max;
        }
        features[b] = _q(sum / n * 10);
        features[SOUND_BANDS + b] = _q(max * 10);
    }

    float c_sum = 0, c_sum2 = 0, flat = 0, p_sum = 0, p_sum2 = 0;
    unsigned onsets = 0;
    for (unsigned i = 0; i < n; i++) {
        c_sum += frames[i].centroid;
        c_sum2 += frames[i].centroid * frames[i].centroid;
        flat += frames[i].flatness;
        p_sum += frames[i].peak_hz;
        p_sum2 += frames[i].peak_hz * frames[i].peak_hz;
        if (i > 0 && frames[i].level > frames[i - 1].level + 6.0f) {
            onsets++;
        }
    }
    float c_mean = c_sum / n;
    float c_var = c_sum2 / n - c_mean * c_mean;
    float p_mean = p_sum / n;
    float p_var = p_sum2 / n - p_mean * p_mean;
    float decay = peak + 1 < n ?
                  (ref - frames[n - 1].level) / (n - 1 - peak) : 0;

    int16_t *f = &features[2 * SOUND_BANDS];
    f[0] = (int16_t)n;
    f[1] = (int16_t)peak;
    f[2] = _q(decay * 10);
    f[3] = (int16_t)onsets;
    f[4] = _q(c_mean);
    f[5] = _q(sqrtf(c_var > 0 ? c_var : 0));
    f[6] = _q(flat / n * 1000);
    f[7] = _q(sqrtf(p_var > 0 ? p_var : 0));
    return 0;
}

int sound_features_pcm(const int16_t *pcm, unsigned n_frames, int16_t *features)
{
    static sound_frame_t frames[SOUND_MAX_FRAMES];

    n_frames = n_frames > SOUND_MAX_FRAMES ? SOUND_MAX_FRAMES : n_frames;
    for (unsigned i = 0; i < n_frames; i++) {
        sound_analyze(&pcm[i * SOUND_FRAME], &frames[i]);
    }
    return sound_features(frames, n_frames, features);
}

unsigned sound_find_event(const int16_t *pcm, unsigned n_frames, unsigned *first)
{
    sound_frame_t frame;

    sound_init();
    for (unsigned i = 0; i < n_frames; i++) {
        sound_analyze(&pcm[i * SOUND_FRAME], &frame);
        if (sound_detect(&frame) == SOUND_END) {
            return sound_event(first);
        }
    }
    return 0;
}
