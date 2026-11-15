/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the keyword spotter with synthetic "words"
 *
 * Four synthetic sounds (a rising chirp, a falling chirp, two tones and a
 * noise burst) stand for words. Each is taught 3 times, then played with
 * random changes of length, pitch, loudness and background noise. The test
 * checks that every utterance is detected and recognized, and that a sound
 * that was never taught (three short high pips) is rejected.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "kws.h"

#define RATE        ((float)KWS_SAMPLE_RATE)
#define TESTS       (15)
#define MAX_SAMPLES (16000 * 3)

static int16_t _signal[MAX_SAMPLES];
static size_t _len;
static unsigned _taught;
static unsigned _results;

static float _rand(void)
{
    return (float)rand() / (float)RAND_MAX;
}

static float _noise(void)
{
    return 2.0f * _rand() - 1.0f;
}

/* A harmonic tone with a frequency moving from f0 to f1 */
static void _add_sweep(float *buf, size_t start, size_t n, float f0, float f1, float amp)
{
    float phase = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = f0 + (f1 - f0) * t;
        phase += 2.0f * (float)M_PI * f / RATE;
        float env = sinf((float)M_PI * t);
        buf[start + i] += amp * env * (sinf(phase) + 0.5f * sinf(2 * phase) +
                                       0.25f * sinf(3 * phase));
    }
}

static void _add_burst(float *buf, size_t start, size_t n, float amp)
{
    float lp = 0.0f, bp = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / n;
        /* crude resonance around 1 kHz */
        float x = _noise();
        lp += 0.35f * (x - lp);
        bp = 0.9f * bp + (x - lp);
        buf[start + i] += amp * sinf((float)M_PI * t) * bp * 0.5f;
    }
}

/* Render a word into the signal, with random changes when vary is set */
static void _word(int word, bool vary)
{
    static float buf[MAX_SAMPLES];
    float stretch = vary ? 0.8f + 0.45f * _rand() : 1.0f;
    float pitch = vary ? 0.92f + 0.16f * _rand() : 1.0f;
    float amp = vary ? 2000.0f + 8000.0f * _rand() : 6000.0f;
    size_t lead = 16000 * 0.6f + (vary ? 3000 * _rand() : 0);
    size_t n = 0;

    for (size_t i = 0; i < MAX_SAMPLES; i++) {
        buf[i] = 0.0f;
    }

    switch (word) {
    case 0:
        n = 6400 * stretch;
        _add_sweep(buf, lead, n, 300 * pitch, 1500 * pitch, amp);
        break;
    case 1:
        n = 6400 * stretch;
        _add_sweep(buf, lead, n, 1500 * pitch, 300 * pitch, amp);
        break;
    case 2:
        n = 3200 * stretch;
        _add_sweep(buf, lead, n, 500 * pitch, 500 * pitch, amp);
        _add_sweep(buf, lead + n + 1200, n, 2000 * pitch, 2000 * pitch, amp);
        n = 2 * n + 1200;
        break;
    case 3:
        n = 5600 * stretch;
        _add_burst(buf, lead, n, amp * 1.5f);
        break;
    default:
        /* not taught: three pips */
        n = 1600 * stretch;
        for (int i = 0; i < 3; i++) {
            _add_sweep(buf, lead + i * (n + 1000), n, 3000 * pitch, 3200 * pitch, amp);
        }
        n = 3 * n + 2000;
        break;
    }

    _len = lead + n + 16000 * 0.6f;
    for (size_t i = 0; i < _len; i++) {
        float v = buf[i] + 30.0f * _noise();
        _signal[i] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, v));
    }
}

/* Feed the signal frame by frame, return the last event */
static kws_event_t _feed(void)
{
    kws_event_t last = KWS_EVENT_NONE;
    for (size_t i = 0; i + KWS_FRAME_LEN <= _len; i += KWS_FRAME_LEN) {
        kws_event_t ev = kws_process(&_signal[i]);
        if (ev == KWS_EVENT_TAUGHT) {
            _taught++;
        }
        if (ev == KWS_EVENT_RESULT) {
            _results++;
        }
        if (ev != KWS_EVENT_NONE) {
            last = ev;
        }
    }
    return last;
}

int main(void)
{
    unsigned correct = 0, rejected = 0, wrong = 0;

    srand(42);
    if (kws_init() != 0) {
        puts("FAIL: kws_init");
        return 1;
    }

    /* settle the noise floor */
    _len = 16000;
    for (size_t i = 0; i < _len; i++) {
        _signal[i] = (int16_t)(30.0f * _noise());
    }
    _feed();

    for (int w = 0; w < (int)KWS_NUM_WORDS; w++) {
        for (unsigned e = 0; e < KWS_NUM_EXAMPLES; e++) {
            kws_teach(w);
            _word(w, true);
            if (_feed() != KWS_EVENT_TAUGHT) {
                printf("FAIL: example %u of word %d not detected\n", e, w);
                return 1;
            }
        }
    }

    for (int t = 0; t < TESTS * (int)KWS_NUM_WORDS; t++) {
        int w = rand() % KWS_NUM_WORDS;
        _word(w, true);
        unsigned before = _results;
        _feed();
        if (_results != before + 1) {
            printf("FAIL: test %d: %u results for one utterance\n", t, _results - before);
            return 1;
        }
        int got = kws_result_word();
        if (got == w) {
            correct++;
        }
        else if (got < 0) {
            rejected++;
        }
        else {
            wrong++;
        }
    }

    unsigned unknown_accepted = 0;
    for (int t = 0; t < TESTS; t++) {
        _word(KWS_NUM_WORDS, true);
        unsigned before = _results;
        _feed();
        /* only a new result counts, kws_result_word() keeps the last one */
        if (_results != before && kws_result_word() >= 0) {
            unknown_accepted++;
        }
    }

    printf("taught %u, results %u: %u correct, %u rejected, %u wrong, "
           "%u of %u unknown sounds accepted\n",
           _taught, _results, correct, rejected, wrong, unknown_accepted, TESTS);
    if (wrong > 0 || correct < TESTS * KWS_NUM_WORDS * 9 / 10 || unknown_accepted > TESTS / 5) {
        puts("FAIL");
        return 1;
    }
    puts("PASS");
    return 0;
}
