/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Keyword spotting taught by example
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "eml_audio.h"

#include "kws.h"

#define FRAMES_PER_FEAT (2U)            /* features every 32 ms */
#define NUM_BINS        (KWS_FRAME_LEN / 2 + 1)
#define NUM_MELS        (20U)
#define MEL_FMIN        (100.0f)
#define MEL_FMAX        (7000.0f)

#define MAX_FEATS       (40U)           /* 1.28 s */
#define MIN_FEATS       (7U)            /* 224 ms */
#define PREROLL         (3U)

/* Voice activity: start and stop levels above the noise floor, in dB */
#define VAD_START_DB    (10.0f)
#define VAD_STOP_DB     (5.0f)
#define VAD_START_FEATS (2U)
#define VAD_STOP_FEATS  (8U)

typedef struct {
    unsigned len;
    float feats[MAX_FEATS][NUM_MELS];
} utterance_t;

static float _re[KWS_FRAME_LEN];
static float _im[KWS_FRAME_LEN];
static float _power[NUM_BINS];
static float _fft_sin[KWS_FRAME_LEN / 2];
static float _fft_cos[KWS_FRAME_LEN / 2];
static const EmlFFT _fft = { KWS_FRAME_LEN / 2, _fft_sin, _fft_cos };
static float _window[KWS_FRAME_LEN];
static float _mel_weights[NUM_MELS][NUM_BINS];

static utterance_t _examples[KWS_NUM_WORDS][KWS_NUM_EXAMPLES];
static unsigned _num_examples[KWS_NUM_WORDS];
static unsigned _next_example[KWS_NUM_WORDS];
static float _limit[KWS_NUM_WORDS];
static utterance_t _current;

/* processing state */
static float _acc[NUM_MELS];
static unsigned _frames;
static float _preroll[PREROLL][NUM_MELS];
static unsigned _preroll_count;
static float _floor_db = 200.0f;
static unsigned _above;
static unsigned _below;
static float _prev_x;
static float _prev_y;

/* results */
static volatile float _level_db;
static volatile bool _in_speech;
static volatile int _teach_word = -1;
static volatile int _result_word = -1;
static volatile float _result_dist;

int kws_init(void)
{
    static float unit[NUM_BINS];
    static float mels[NUM_MELS];
    const EmlAudioMel params = {
        .n_mels = NUM_MELS, .fmin = MEL_FMIN, .fmax = MEL_FMAX,
        .n_fft = KWS_FRAME_LEN, .samplerate = KWS_SAMPLE_RATE,
    };

    eml_fft_fill(_fft, KWS_FRAME_LEN);
    for (unsigned i = 0; i < KWS_FRAME_LEN; i++) {
        _window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (KWS_FRAME_LEN - 1));
    }

    /* the filterbank matrix, from unit spectra passed through the emlearn
       mel filterbank */
    for (unsigned k = 0; k < NUM_BINS; k++) {
        memset(unit, 0, sizeof(unit));
        unit[k] = 1.0f;
        EmlError err = eml_audio_melspec(params, (EmlVector){ unit, NUM_BINS },
                                         (EmlVector){ mels, NUM_MELS });
        if (err != EmlOk) {
            return -1;
        }
        for (unsigned m = 0; m < NUM_MELS; m++) {
            _mel_weights[m][k] = mels[m];
        }
    }
    return 0;
}

/* Mel band powers of one frame, added to the accumulator */
static void _frame_mels(const int16_t *samples)
{
    for (unsigned i = 0; i < KWS_FRAME_LEN; i++) {
        /* DC blocking high-pass filter */
        float x = samples[i];
        float y = x - _prev_x + 0.995f * _prev_y;
        _prev_x = x;
        _prev_y = y;
        _re[i] = y * _window[i];
        _im[i] = 0.0f;
    }
    eml_fft_forward(_fft, _re, _im, KWS_FRAME_LEN);
    for (unsigned k = 0; k < NUM_BINS; k++) {
        _power[k] = _re[k] * _re[k] + _im[k] * _im[k];
    }
    for (unsigned m = 0; m < NUM_MELS; m++) {
        float sum = 0.0f;
        for (unsigned k = 0; k < NUM_BINS; k++) {
            sum += _mel_weights[m][k] * _power[k];
        }
        _acc[m] += sum;
    }
}

static float _frame_dist(const float *a, const float *b)
{
    float sum = 0.0f;
    for (unsigned m = 0; m < NUM_MELS; m++) {
        float d = a[m] - b[m];
        sum += d * d;
    }
    return sqrtf(sum);
}

/* Dynamic time warping distance, normalized by the path length */
static float _dtw(const utterance_t *a, const utterance_t *b)
{
    static float prev[MAX_FEATS + 1];
    static float cur[MAX_FEATS + 1];
    const float inf = 1e30f;

    for (unsigned j = 0; j <= b->len; j++) {
        prev[j] = inf;
    }
    prev[0] = 0.0f;

    for (unsigned i = 1; i <= a->len; i++) {
        cur[0] = inf;
        for (unsigned j = 1; j <= b->len; j++) {
            float best = prev[j - 1];
            if (prev[j] < best) {
                best = prev[j];
            }
            if (cur[j - 1] < best) {
                best = cur[j - 1];
            }
            cur[j] = best + _frame_dist(a->feats[i - 1], b->feats[j - 1]);
        }
        memcpy(prev, cur, sizeof(float) * (b->len + 1));
    }
    return prev[b->len] / (float)(a->len + b->len);
}

/* Remove the mean of each band: makes the match independent of the gain */
static void _normalize(utterance_t *u)
{
    for (unsigned m = 0; m < NUM_MELS; m++) {
        float mean = 0.0f;
        for (unsigned t = 0; t < u->len; t++) {
            mean += u->feats[t][m];
        }
        mean /= u->len;
        for (unsigned t = 0; t < u->len; t++) {
            u->feats[t][m] -= mean;
        }
    }
}

static void _classify(const utterance_t *u)
{
    float word_dist[KWS_NUM_WORDS];
    int best = -1;
    int second = -1;

    for (unsigned w = 0; w < KWS_NUM_WORDS; w++) {
        if (_num_examples[w] == 0) {
            continue;
        }
        word_dist[w] = 1e30f;
        for (unsigned e = 0; e < _num_examples[w]; e++) {
            float d = _dtw(u, &_examples[w][e]);
            if (d < word_dist[w]) {
                word_dist[w] = d;
            }
        }
        if (best < 0 || word_dist[w] < word_dist[best]) {
            second = best;
            best = (int)w;
        }
        else if (second < 0 || word_dist[w] < word_dist[second]) {
            second = (int)w;
        }
    }

    _result_word = -1;
    _result_dist = 0.0f;
    if (best < 0) {
        return;
    }

    bool accepted = word_dist[best] < _limit[best];
    if (second >= 0 && word_dist[best] > KWS_MARGIN * word_dist[second]) {
        accepted = false;
    }
    printf("kws: word %d at %d.%03d (limit %d.%03d)", best, (int)word_dist[best],
           (int)(word_dist[best] * 1000) % 1000, (int)_limit[best],
           (int)(_limit[best] * 1000) % 1000);
    if (second >= 0) {
        printf(", then word %d at %d.%03d", second, (int)word_dist[second],
               (int)(word_dist[second] * 1000) % 1000);
    }
    printf(": %s\n", accepted ? "accepted" : "rejected");

    _result_dist = word_dist[best];
    _result_word = accepted ? best : -1;
}

/* Acceptance limit of a word from the spread of its examples */
static void _update_limit(unsigned w)
{
    float sum = 0.0f;
    unsigned pairs = 0;

    for (unsigned a = 0; a < _num_examples[w]; a++) {
        for (unsigned b = a + 1; b < _num_examples[w]; b++) {
            sum += _dtw(&_examples[w][a], &_examples[w][b]);
            pairs++;
        }
    }
    _limit[w] = pairs ? KWS_SPREAD_FACTOR * sum / pairs : KWS_MAX_DIST;
}

static kws_event_t _utterance_done(void)
{
    if (_current.len < MIN_FEATS) {
        return KWS_EVENT_NONE;
    }
    _normalize(&_current);

    int teach = _teach_word;
    if (teach >= 0) {
        unsigned slot = _next_example[teach];
        _examples[teach][slot] = _current;
        _next_example[teach] = (slot + 1) % KWS_NUM_EXAMPLES;
        if (_num_examples[teach] < KWS_NUM_EXAMPLES) {
            _num_examples[teach]++;
        }
        _update_limit(teach);
        printf("kws: example %u of word %d, %u features\n", _num_examples[teach], teach,
               _current.len);
        _teach_word = -1;
        return KWS_EVENT_TAUGHT;
    }
    _classify(&_current);
    return KWS_EVENT_RESULT;
}

kws_event_t kws_process(const int16_t *frame)
{
    _frame_mels(frame);
    if (++_frames < FRAMES_PER_FEAT) {
        return KWS_EVENT_NONE;
    }
    _frames = 0;

    /* log mel feature and energy of the last 32 ms */
    float feat[NUM_MELS];
    float total = 0.0f;
    for (unsigned m = 0; m < NUM_MELS; m++) {
        total += _acc[m];
        feat[m] = log10f(_acc[m] + 1.0f);
    }
    memset(_acc, 0, sizeof(_acc));
    float db = 10.0f * log10f(total + 1.0f);

    /* the noise floor follows the quiet parts at once, the loud parts
       slowly, and stays frozen during an utterance */
    if (!_in_speech) {
        if (db < _floor_db) {
            _floor_db = db;
        }
        else {
            _floor_db += 0.05f;
        }
    }
    _level_db = db - _floor_db;

    if (!_in_speech) {
        memmove(_preroll[0], _preroll[1], sizeof(_preroll[0]) * (PREROLL - 1));
        memcpy(_preroll[PREROLL - 1], feat, sizeof(feat));
        if (_preroll_count < PREROLL) {
            _preroll_count++;
        }
        _above = (db > _floor_db + VAD_START_DB) ? _above + 1 : 0;
        if (_above >= VAD_START_FEATS) {
            _in_speech = true;
            _below = 0;
            /* start with the frames before the onset */
            _current.len = 0;
            for (unsigned i = PREROLL - _preroll_count; i < PREROLL; i++) {
                memcpy(_current.feats[_current.len++], _preroll[i], sizeof(feat));
            }
        }
        return KWS_EVENT_NONE;
    }

    memcpy(_current.feats[_current.len++], feat, sizeof(feat));
    _below = (db < _floor_db + VAD_STOP_DB) ? _below + 1 : 0;
    if (_below >= VAD_STOP_FEATS || _current.len >= MAX_FEATS) {
        /* drop most of the trailing silence */
        if (_below >= VAD_STOP_FEATS) {
            _current.len -= VAD_STOP_FEATS - 2;
        }
        _in_speech = false;
        _above = 0;
        _preroll_count = 0;
        return _utterance_done();
    }
    return KWS_EVENT_NONE;
}

void kws_restart(void)
{
    _in_speech = false;
    _above = 0;
    _below = 0;
    _preroll_count = 0;
    _current.len = 0;
}

void kws_teach(int word)
{
    _teach_word = (word >= 0 && word < (int)KWS_NUM_WORDS) ? word : -1;
}

int kws_teaching(void)
{
    return _teach_word;
}

unsigned kws_num_examples(unsigned word)
{
    return (word < KWS_NUM_WORDS) ? _num_examples[word] : 0;
}

int kws_result_word(void)
{
    return _result_word;
}

float kws_result_dist(void)
{
    return _result_dist;
}

float kws_level_db(void)
{
    return _level_db;
}

bool kws_in_speech(void)
{
    return _in_speech;
}
