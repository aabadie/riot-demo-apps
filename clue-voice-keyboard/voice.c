/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Word recognition taught by example, for a large vocabulary
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "eml_audio.h"

#include "voice.h"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define FRAMES_PER_FEAT (2U)            /* features every 32 ms */
#define NUM_BINS        (VOICE_FRAME_LEN / 2 + 1)
#define NUM_MELS        VOICE_NUM_MELS
#define MAX_FEATS       VOICE_MAX_FEATS
#define MEL_FMIN        (100.0f)
#define MEL_FMAX        (7000.0f)

#define MIN_FEATS       (7U)            /* 224 ms */
#define PREROLL         (3U)

/* 8 bit features: log10 of the band power, mean removed, times QSCALE */
#define QSCALE          (24.0f)

/* Voice activity: start and stop levels above the noise floor, in dB */
#define VAD_START_DB    (10.0f)
#define VAD_STOP_DB     (5.0f)
#define VAD_START_FEATS (2U)
#define VAD_STOP_FEATS  (8U)

#define INF             (1e30f)

typedef struct {
    unsigned len;
    float feats[MAX_FEATS][NUM_MELS];
} utterance_t;

static float _re[VOICE_FRAME_LEN];
static float _im[VOICE_FRAME_LEN];
static float _power[NUM_BINS];
static float _fft_sin[VOICE_FRAME_LEN / 2];
static float _fft_cos[VOICE_FRAME_LEN / 2];
static const EmlFFT _fft = { VOICE_FRAME_LEN / 2, _fft_sin, _fft_cos };
static float _window[VOICE_FRAME_LEN];
static float _mel_weights[NUM_MELS][NUM_BINS];

static voice_store_t _store;
static utterance_t _current;
static voice_example_t _current_q;

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
static volatile int _result_best = -1;
static volatile float _result_dist;

void voice_forget_all(void)
{
    memset(&_store, 0, sizeof(_store));
    _store.magic = VOICE_MAGIC;
    for (unsigned w = 0; w < VOICE_NUM_WORDS; w++) {
        _store.limit[w] = VOICE_MAX_DIST;
    }
}

int voice_init(void)
{
    static float unit[NUM_BINS];
    static float mels[NUM_MELS];
    const EmlAudioMel params = {
        .n_mels = NUM_MELS, .fmin = MEL_FMIN, .fmax = MEL_FMAX,
        .n_fft = VOICE_FRAME_LEN, .samplerate = VOICE_SAMPLE_RATE,
    };

    voice_forget_all();
    eml_fft_fill(_fft, VOICE_FRAME_LEN);
    for (unsigned i = 0; i < VOICE_FRAME_LEN; i++) {
        _window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (VOICE_FRAME_LEN - 1));
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
    for (unsigned i = 0; i < VOICE_FRAME_LEN; i++) {
        /* DC blocking high-pass filter */
        float x = samples[i];
        float y = x - _prev_x + 0.995f * _prev_y;
        _prev_x = x;
        _prev_y = y;
        _re[i] = y * _window[i];
        _im[i] = 0.0f;
    }
    eml_fft_forward(_fft, _re, _im, VOICE_FRAME_LEN);
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

static float _frame_dist(const int8_t *a, const int8_t *b)
{
    int32_t sum = 0;

    for (unsigned m = 0; m < NUM_MELS; m++) {
        int32_t d = (int32_t)a[m] - b[m];
        sum += d * d;
    }
    return sqrtf((float)sum);
}

/* Dynamic time warping distance, normalized by the path length. The path
   stays in a band around the diagonal, and utterances of very different
   lengths do not match at all: this keeps the cost low with many words. */
static float _dtw(const voice_example_t *a, const voice_example_t *b)
{
    static float prev[MAX_FEATS + 1];
    static float cur[MAX_FEATS + 1];
    unsigned la = a->len;
    unsigned lb = b->len;

    if (la == 0 || lb == 0 || la * 5 > lb * 12 || lb * 5 > la * 12) {
        return INF;
    }
    for (unsigned j = 0; j <= lb; j++) {
        prev[j] = INF;
    }
    prev[0] = 0.0f;

    for (unsigned i = 1; i <= la; i++) {
        unsigned center = (i * lb + la / 2) / la;
        unsigned lo = center > VOICE_BAND + 1 ? center - VOICE_BAND : 1;
        unsigned hi = center + VOICE_BAND < lb ? center + VOICE_BAND : lb;

        for (unsigned j = 0; j <= lb; j++) {
            cur[j] = INF;
        }
        for (unsigned j = lo; j <= hi; j++) {
            float best = prev[j - 1];
            if (prev[j] < best) {
                best = prev[j];
            }
            if (cur[j - 1] < best) {
                best = cur[j - 1];
            }
            if (best < INF) {
                cur[j] = best + _frame_dist(a->feats[i - 1], b->feats[j - 1]);
            }
        }
        memcpy(prev, cur, sizeof(float) * (lb + 1));
    }
    if (prev[lb] >= INF) {
        return INF;
    }
    return prev[lb] / (QSCALE * (float)(la + lb));
}

/* Remove the mean of each band, which makes the match independent of the
   gain, then scale to 8 bits */
static void _quantize(const utterance_t *u, voice_example_t *q)
{
    q->len = (uint8_t)u->len;
    for (unsigned m = 0; m < NUM_MELS; m++) {
        float mean = 0.0f;
        for (unsigned t = 0; t < u->len; t++) {
            mean += u->feats[t][m];
        }
        mean /= u->len;
        for (unsigned t = 0; t < u->len; t++) {
            float v = (u->feats[t][m] - mean) * QSCALE;
            v = v > 127.0f ? 127.0f : (v < -127.0f ? -127.0f : v);
            q->feats[t][m] = (int8_t)lrintf(v);
        }
    }
}

static void _classify(const voice_example_t *u)
{
    float word_dist[VOICE_NUM_WORDS];
    int best = -1;
    int second = -1;

    for (unsigned w = 0; w < VOICE_NUM_WORDS; w++) {
        word_dist[w] = INF;
        for (unsigned e = 0; e < _store.num_examples[w]; e++) {
            float d = _dtw(u, &_store.examples[w][e]);
            if (d < word_dist[w]) {
                word_dist[w] = d;
            }
        }
        if (word_dist[w] >= INF) {
            continue;
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
    _result_best = best;
    _result_dist = 0.0f;
    if (best < 0) {
        return;
    }

    bool accepted = word_dist[best] < _store.limit[best];
    if (second >= 0 && word_dist[best] > VOICE_MARGIN * word_dist[second]) {
        accepted = false;
    }
    printf("voice: word %d at %d.%03d (limit %d.%03d)", best, (int)word_dist[best],
           (int)(word_dist[best] * 1000) % 1000, (int)_store.limit[best],
           (int)(_store.limit[best] * 1000) % 1000);
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

    for (unsigned a = 0; a < _store.num_examples[w]; a++) {
        for (unsigned b = a + 1; b < _store.num_examples[w]; b++) {
            float d = _dtw(&_store.examples[w][a], &_store.examples[w][b]);
            if (d < INF) {
                sum += d;
                pairs++;
            }
        }
    }
    _store.limit[w] = pairs ? VOICE_SPREAD_FACTOR * sum / pairs : VOICE_MAX_DIST;
}

static voice_event_t _utterance_done(void)
{
    if (_current.len < MIN_FEATS) {
        return VOICE_EVENT_NONE;
    }
    _quantize(&_current, &_current_q);

    int teach = _teach_word;
    if (teach >= 0) {
        unsigned slot = _store.next_example[teach];
        _store.examples[teach][slot] = _current_q;
        _store.next_example[teach] = (uint8_t)((slot + 1) % VOICE_NUM_EXAMPLES);
        if (_store.num_examples[teach] < VOICE_NUM_EXAMPLES) {
            _store.num_examples[teach]++;
        }
        _update_limit(teach);
        printf("voice: example %u of word %d, %u features\n",
               _store.num_examples[teach], teach, _current.len);
        _teach_word = -1;
        return VOICE_EVENT_TAUGHT;
    }
    _classify(&_current_q);
    return VOICE_EVENT_RESULT;
}

voice_event_t voice_process(const int16_t *frame)
{
    _frame_mels(frame);
    if (++_frames < FRAMES_PER_FEAT) {
        return VOICE_EVENT_NONE;
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
        return VOICE_EVENT_NONE;
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
    return VOICE_EVENT_NONE;
}

void voice_teach(int word)
{
    _teach_word = (word >= 0 && word < (int)VOICE_NUM_WORDS) ? word : -1;
}

int voice_teaching(void)
{
    return _teach_word;
}

unsigned voice_num_examples(unsigned word)
{
    return (word < VOICE_NUM_WORDS) ? _store.num_examples[word] : 0;
}

unsigned voice_num_words(void)
{
    unsigned n = 0;

    for (unsigned w = 0; w < VOICE_NUM_WORDS; w++) {
        n += _store.num_examples[w] > 0;
    }
    return n;
}

int voice_result_word(void)
{
    return _result_word;
}

int voice_result_best(void)
{
    return _result_best;
}

float voice_result_dist(void)
{
    return _result_dist;
}

float voice_level_db(void)
{
    return _level_db;
}

bool voice_in_speech(void)
{
    return _in_speech;
}

const voice_store_t *voice_store(void)
{
    return &_store;
}

int voice_load(const voice_store_t *store)
{
    if (store->magic != VOICE_MAGIC) {
        return -1;
    }
    for (unsigned w = 0; w < VOICE_NUM_WORDS; w++) {
        if (store->num_examples[w] > VOICE_NUM_EXAMPLES ||
            store->next_example[w] >= VOICE_NUM_EXAMPLES) {
            return -1;
        }
        for (unsigned e = 0; e < store->num_examples[w]; e++) {
            if (store->examples[w][e].len > MAX_FEATS) {
                return -1;
            }
        }
    }
    memcpy(&_store, store, sizeof(_store));
    return 0;
}
