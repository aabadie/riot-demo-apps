/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Word recognition taught by example, for a large vocabulary
 *
 * Plain C without RIOT dependencies, so it can be tested on the host.
 * Feed it frames of VOICE_FRAME_LEN samples at 16 kHz; it detects
 * utterances with an adaptive energy threshold, computes log mel features
 * with emlearn and either stores the utterance as an example of a word or
 * matches it against all the examples with dynamic time warping.
 *
 * The examples are kept as 8 bit features in a single structure, so that
 * the application can save it to flash and load it back.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_SAMPLE_RATE   (16000U)    /**< expected sample rate */
#define VOICE_FRAME_LEN     (256U)      /**< samples per frame, 16 ms */
#define VOICE_NUM_EXAMPLES  (3U)        /**< examples kept per word */
#define VOICE_NUM_MELS      (20U)       /**< mel bands per feature */
#define VOICE_MAX_FEATS     (40U)       /**< features per utterance, 1.28 s */

#ifndef VOICE_NUM_WORDS
#define VOICE_NUM_WORDS     (40U)       /**< size of the vocabulary */
#endif

/**
 * @brief   Best word must be closer than this distance to be accepted, used
 *          while the word has a single example
 */
#ifndef VOICE_MAX_DIST
#define VOICE_MAX_DIST      (2.0f)
#endif

/**
 * @brief   With two examples or more, the limit is this factor times the
 *          mean distance between the examples of the word
 */
#ifndef VOICE_SPREAD_FACTOR
#define VOICE_SPREAD_FACTOR (2.0f)
#endif

/**
 * @brief   Best word distance must be below VOICE_MARGIN times the second
 *          best
 */
#ifndef VOICE_MARGIN
#define VOICE_MARGIN        (0.85f)
#endif

/**
 * @brief   Half width of the dynamic time warping band, in features
 */
#ifndef VOICE_BAND
#define VOICE_BAND          (8U)
#endif

/**
 * @brief   What happened while processing a frame
 */
typedef enum {
    VOICE_EVENT_NONE,       /**< nothing new */
    VOICE_EVENT_TAUGHT,     /**< the utterance was stored as an example */
    VOICE_EVENT_RESULT,     /**< the utterance was classified */
} voice_event_t;

/**
 * @brief   One example, log mel features as 8 bit fixed point numbers
 */
typedef struct {
    uint8_t len;                                        /**< features used */
    int8_t feats[VOICE_MAX_FEATS][VOICE_NUM_MELS];      /**< log mel features */
} voice_example_t;

/**
 * @brief   Everything that was taught, saved as is by the application
 */
typedef struct {
    uint32_t magic;                                     /**< VOICE_MAGIC when valid */
    uint8_t num_examples[VOICE_NUM_WORDS];              /**< examples per word */
    uint8_t next_example[VOICE_NUM_WORDS];              /**< slot replaced next */
    float limit[VOICE_NUM_WORDS];                       /**< acceptance distance */
    voice_example_t examples[VOICE_NUM_WORDS][VOICE_NUM_EXAMPLES]; /**< examples */
} voice_store_t;

/**
 * @brief   Magic number of a valid store, changes with its layout
 */
#define VOICE_MAGIC         (0x564b0100UL + VOICE_NUM_WORDS)

/**
 * @brief   Prepare the FFT and the mel filterbank, forget all the words
 *
 * @return  0 on success, -1 when emlearn rejects the mel parameters
 */
int voice_init(void);

/**
 * @brief   Process one frame of VOICE_FRAME_LEN samples
 */
voice_event_t voice_process(const int16_t *frame);

/**
 * @brief   Record the next utterance as an example of @p word, -1 cancels
 */
void voice_teach(int word);

/**
 * @brief   Word waiting for an example, -1 when classifying
 */
int voice_teaching(void);

/**
 * @brief   Number of examples of a word
 */
unsigned voice_num_examples(unsigned word);

/**
 * @brief   Number of words with at least one example
 */
unsigned voice_num_words(void);

/**
 * @brief   Forget all the examples
 */
void voice_forget_all(void);

/**
 * @brief   Word of the last result, -1 when rejected
 */
int voice_result_word(void);

/**
 * @brief   Word closest to the last utterance, even when rejected, -1 when
 *          nothing was taught
 */
int voice_result_best(void);

/**
 * @brief   Distance of the best word in the last result
 */
float voice_result_dist(void);

/**
 * @brief   Current level above the noise floor in dB
 */
float voice_level_db(void);

/**
 * @brief   Tell whether an utterance is in progress
 */
bool voice_in_speech(void);

/**
 * @brief   The examples, to save them
 */
const voice_store_t *voice_store(void);

/**
 * @brief   Load examples saved before
 *
 * @return  0 on success, -1 when @p store is not a valid store
 */
int voice_load(const voice_store_t *store);

#ifdef __cplusplus
}
#endif
