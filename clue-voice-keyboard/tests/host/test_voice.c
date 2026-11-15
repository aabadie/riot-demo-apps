/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the voice keyboard recognition with synthetic
 *              "words"
 *
 * Forty synthetic words are made of two or three syllables: harmonic
 * sweeps between formant-like frequencies, some with a noise burst for a
 * consonant. Each word is taught 3 times, then played with random changes
 * of length, pitch, loudness and background noise. The test checks the
 * recognition rate, that a sound that was never taught is mostly rejected,
 * that the examples survive a save and load, and the key codes.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "keys.h"
#include "voice.h"

#ifndef M_PI
#define M_PI        (3.14159265358979323846)
#endif

#define RATE        ((float)VOICE_SAMPLE_RATE)
#define TESTS       (5)
#define MAX_SAMPLES (16000 * 3)
#define UNKNOWN     (-1)

static int16_t _signal[MAX_SAMPLES];
static size_t _len;
static unsigned _taught;
static unsigned _results;

/* start and end frequencies of the syllables */
static const float _syllables[8][2] = {
    { 300, 900 }, { 900, 300 }, { 500, 500 }, { 1400, 1400 },
    { 700, 2000 }, { 2000, 700 }, { 400, 1200 }, { 2400, 2400 },
};

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
    for (size_t i = 0; i < n && start + i < MAX_SAMPLES; i++) {
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
    float lp = 0.0f;
    for (size_t i = 0; i < n && start + i < MAX_SAMPLES; i++) {
        float t = (float)i / n;
        float x = _noise();
        lp += 0.5f * (x - lp);
        buf[start + i] += amp * sinf((float)M_PI * t) * (x - lp);
    }
}

/* Render a word into the signal, with random changes */
static void _word(int word)
{
    static float buf[MAX_SAMPLES];
    float stretch = 0.85f + 0.3f * _rand();
    float pitch = 0.95f + 0.1f * _rand();
    float amp = 2000.0f + 8000.0f * _rand();
    size_t pos = 16000 * 0.6f + 3000 * _rand();
    int syl[3];
    int count;

    memset(buf, 0, sizeof(buf));
    if (word == UNKNOWN) {
        /* never taught: three quick high pips */
        size_t n = 1200 * stretch;
        for (int i = 0; i < 3; i++) {
            _add_sweep(buf, pos, n, 3000 * pitch, 3300 * pitch, amp);
            pos += n + 800;
        }
    }
    else {
        /* 40 distinct sequences of syllables */
        syl[0] = word % 8;
        syl[1] = (word / 8 + word % 8 + 1) % 8;
        syl[2] = (syl[0] + 3 * syl[1] + 2) % 8;
        count = (word % 3 == 0) ? 3 : 2;
        if ((word / 8) % 2) {
            size_t n = 900 * stretch;
            _add_burst(buf, pos, n, amp * 1.5f);
            pos += n;
        }
        for (int s = 0; s < count; s++) {
            size_t n = 2800 * stretch;
            _add_sweep(buf, pos, n, _syllables[syl[s]][0] * pitch,
                       _syllables[syl[s]][1] * pitch, amp);
            pos += n + 600 * stretch;
        }
    }

    _len = pos + 16000 * 0.6f;
    if (_len > MAX_SAMPLES) {
        _len = MAX_SAMPLES;
    }
    for (size_t i = 0; i < _len; i++) {
        float v = buf[i] + 30.0f * _noise();
        _signal[i] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, v));
    }
}

/* Feed the signal frame by frame, return the last event */
static voice_event_t _feed(void)
{
    voice_event_t last = VOICE_EVENT_NONE;
    for (size_t i = 0; i + VOICE_FRAME_LEN <= _len; i += VOICE_FRAME_LEN) {
        voice_event_t ev = voice_process(&_signal[i]);
        if (ev == VOICE_EVENT_TAUGHT) {
            _taught++;
        }
        if (ev == VOICE_EVENT_RESULT) {
            _results++;
        }
        if (ev != VOICE_EVENT_NONE) {
            last = ev;
        }
    }
    return last;
}

typedef struct {
    unsigned correct, rejected, wrong, unknown_accepted;
    double ms_per_word;
} score_t;

static int _evaluate(score_t *score)
{
    clock_t start = clock();
    memset(score, 0, sizeof(*score));

    for (int t = 0; t < TESTS * (int)VOICE_NUM_WORDS; t++) {
        int w = t % VOICE_NUM_WORDS;
        _word(w);
        unsigned before = _results;
        _feed();
        if (_results != before + 1) {
            printf("FAIL: test %d: %u results for one utterance\n", t, _results - before);
            return -1;
        }
        int got = voice_result_word();
        if (got == w) {
            score->correct++;
        }
        else if (got < 0) {
            score->rejected++;
        }
        else {
            score->wrong++;
        }
    }
    score->ms_per_word = 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC /
                         (TESTS * VOICE_NUM_WORDS);

    for (int t = 0; t < TESTS * 4; t++) {
        _word(UNKNOWN);
        _feed();
        if (voice_result_word() >= 0) {
            score->unknown_accepted++;
        }
    }
    return 0;
}

static int _test_keys(void)
{
    uint8_t mod, usage;

    if (!keys_lookup('a', &mod, &usage) || usage != 0x04 || mod != 0 ||
        !keys_lookup('Z', &mod, &usage) || usage != 0x1d || mod != KEYS_MOD_SHIFT ||
        !keys_lookup('1', &mod, &usage) || usage != 0x1e || mod != 0 ||
        !keys_lookup('0', &mod, &usage) || usage != 0x27 ||
        !keys_lookup(' ', &mod, &usage) || usage != 0x2c ||
        !keys_lookup(KEYS_CHAR_ENTER, &mod, &usage) || usage != 0x28 ||
        !keys_lookup(KEYS_CHAR_DELETE, &mod, &usage) || usage != 0x2a ||
        keys_lookup(KEYS_CHAR_SHIFT, &mod, &usage)) {
        puts("FAIL: key codes");
        return -1;
    }
    for (unsigned w = 0; w < KEYS_NUM_WORDS; w++) {
        for (unsigned v = w + 1; v < KEYS_NUM_WORDS; v++) {
            if (keys_words[w].ch == keys_words[v].ch) {
                printf("FAIL: words %s and %s type the same key\n",
                       keys_words[w].name, keys_words[v].name);
                return -1;
            }
        }
    }
    return 0;
}

int main(void)
{
    static voice_store_t saved;
    score_t score, reloaded;

    if (_test_keys() != 0) {
        return 1;
    }
    srand(42);
    if (voice_init() != 0) {
        puts("FAIL: voice_init");
        return 1;
    }

    /* settle the noise floor */
    _len = 16000;
    for (size_t i = 0; i < _len; i++) {
        _signal[i] = (int16_t)(30.0f * _noise());
    }
    _feed();

    for (int w = 0; w < (int)VOICE_NUM_WORDS; w++) {
        for (unsigned e = 0; e < VOICE_NUM_EXAMPLES; e++) {
            voice_teach(w);
            _word(w);
            if (_feed() != VOICE_EVENT_TAUGHT) {
                printf("FAIL: example %u of word %d not detected\n", e, w);
                return 1;
            }
        }
    }
    if (voice_num_words() != VOICE_NUM_WORDS) {
        puts("FAIL: not all words taught");
        return 1;
    }

    if (_evaluate(&score) != 0) {
        return 1;
    }
    printf("taught %u, results %u: %u correct, %u rejected, %u wrong, "
           "%u of %u unknown sounds accepted, %.1f ms per word on the host\n",
           _taught, _results, score.correct, score.rejected, score.wrong,
           score.unknown_accepted, TESTS * 4, score.ms_per_word);

    /* save, forget, load: same results */
    memcpy(&saved, voice_store(), sizeof(saved));
    voice_forget_all();
    if (voice_num_words() != 0) {
        puts("FAIL: forget_all");
        return 1;
    }
    saved.magic ^= 1;
    if (voice_load(&saved) == 0) {
        puts("FAIL: a bad store was loaded");
        return 1;
    }
    saved.magic ^= 1;
    if (voice_load(&saved) != 0 || voice_num_words() != VOICE_NUM_WORDS) {
        puts("FAIL: voice_load");
        return 1;
    }
    srand(43);
    if (_evaluate(&reloaded) != 0) {
        return 1;
    }
    printf("after reload: %u correct, %u rejected, %u wrong\n", reloaded.correct,
           reloaded.rejected, reloaded.wrong);
    printf("store: %zu bytes\n", sizeof(voice_store_t));

    /* a word taught while the word to teach changes in its middle is
       dropped */
    _word(5);
    unsigned before = voice_num_examples(7);
    voice_teach(5);
    size_t half = _len / 2;
    size_t full = _len;
    _len = half - half % VOICE_FRAME_LEN;
    _feed();
    voice_teach(7);
    memmove(_signal, _signal + _len, (full - _len) * sizeof(_signal[0]));
    _len = full - _len;
    voice_event_t changed = _feed();
    voice_teach(-1);
    if (changed != VOICE_EVENT_NONE || voice_num_examples(7) != before) {
        puts("FAIL: the word changed during the utterance, it was still taught");
        return 1;
    }
    puts("word changed during the utterance: dropped");

    /* the noise gets 20 dB louder and stays so: at most one utterance, then
       the floor follows */
    unsigned events = _taught + _results;
    _len = MAX_SAMPLES;
    for (size_t i = 0; i < _len; i++) {
        _signal[i] = (int16_t)(300.0f * _noise());
    }
    _feed();
    _feed();
    unsigned loud = _taught + _results - events;
    if (loud > 1) {
        printf("FAIL: %u utterances in steady loud noise\n", loud);
        return 1;
    }
    printf("steady loud noise: %u utterance\n", loud);

    unsigned total = TESTS * VOICE_NUM_WORDS;
    if (score.wrong > total / 50 || score.correct < total * 9 / 10 ||
        reloaded.correct < total * 9 / 10 || score.unknown_accepted > TESTS) {
        puts("FAIL");
        return 1;
    }
    puts("PASS");
    return 0;
}
