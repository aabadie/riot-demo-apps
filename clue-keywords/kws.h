/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Keyword spotting taught by example
 *
 * Plain C without RIOT dependencies, so it can be tested on the host.
 * Feed it frames of KWS_FRAME_LEN samples at 16 kHz; it detects utterances
 * with an adaptive energy threshold, computes log mel features with emlearn
 * and either stores the utterance as an example of a word or matches it
 * against the examples with dynamic time warping.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KWS_SAMPLE_RATE     (16000U)    /**< expected sample rate */
#define KWS_FRAME_LEN       (256U)      /**< samples per frame, 16 ms */
#define KWS_NUM_WORDS       (4U)        /**< number of words */
#define KWS_NUM_EXAMPLES    (3U)        /**< examples kept per word */

/**
 * @brief   Best word must be closer than this distance to be accepted, used
 *          while the word has a single example
 */
#ifndef KWS_MAX_DIST
#define KWS_MAX_DIST        (2.0f)
#endif

/**
 * @brief   With two examples or more, the limit is this factor times the
 *          mean distance between the examples of the word
 */
#ifndef KWS_SPREAD_FACTOR
#define KWS_SPREAD_FACTOR   (2.2f)
#endif

/**
 * @brief   Best word distance must be below KWS_MARGIN times the second best
 */
#ifndef KWS_MARGIN
#define KWS_MARGIN          (0.75f)
#endif

/**
 * @brief   What happened while processing a frame
 */
typedef enum {
    KWS_EVENT_NONE,         /**< nothing new */
    KWS_EVENT_TAUGHT,       /**< the utterance was stored as an example */
    KWS_EVENT_RESULT,       /**< the utterance was classified */
} kws_event_t;

/**
 * @brief   Prepare the FFT and the mel filterbank
 *
 * @return  0 on success, -1 when emlearn rejects the mel parameters
 */
int kws_init(void);

/**
 * @brief   Process one frame of KWS_FRAME_LEN samples
 */
kws_event_t kws_process(const int16_t *frame);

/**
 * @brief   Record the next utterance as an example of @p word, -1 cancels
 */
void kws_teach(int word);

/**
 * @brief   Drop the utterance in progress, for example while the speaker
 *          plays: the noise floor is kept
 */
void kws_restart(void);

/**
 * @brief   Word waiting for an example, -1 when classifying
 */
int kws_teaching(void);

/**
 * @brief   Number of examples of a word
 */
unsigned kws_num_examples(unsigned word);

/**
 * @brief   Word of the last result, -1 when rejected
 */
int kws_result_word(void);

/**
 * @brief   Distance of the best word in the last result
 */
float kws_result_dist(void);

/**
 * @brief   Current level above the noise floor in dB
 */
float kws_level_db(void);

/**
 * @brief   Tell whether an utterance is in progress
 */
bool kws_in_speech(void);

#ifdef __cplusplus
}
#endif
