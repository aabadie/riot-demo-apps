/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Interference detection on the 16 channels of IEEE 802.15.4
 *
 * Plain C without RIOT dependencies, so it can be tested on the host.
 *
 * A scan gives the occupancy of each channel between 0 (always clear) and
 * 1 (always busy). While learning, the scans are stored; then a Gaussian
 * mixture with diagonal covariances is fitted to them by expectation
 * maximization, and emlearn scores each new scan against it. A low
 * likelihood means a scan that does not look like the learned ones. The
 * channels that differ most from what was learned tell the kind of
 * interference: one or two channels for an IEEE 802.15.4 transmitter, three
 * or more adjacent channels for a wider signal such as Wi-Fi.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DET_CHANNELS        (16U)       /**< channels 11 to 26 */
#define DET_FIRST_CHANNEL   (11U)       /**< first channel */
#define DET_COMPONENTS      (3U)        /**< Gaussian mixture components */
#define DET_TRAIN_MAX       (240U)      /**< scans kept for learning */

/**
 * @brief   Level from which a scan is unusual
 */
#ifndef DET_ALARM_LEVEL
#define DET_ALARM_LEVEL     (3.0f)
#endif

/**
 * @brief   A channel is part of the interference when its occupancy is this
 *          many standard deviations above what was learned
 */
#ifndef DET_CHANNEL_Z
#define DET_CHANNEL_Z       (4.0f)
#endif

/**
 * @brief   Kind of interference
 */
typedef enum {
    DET_NONE,               /**< nothing unusual */
    DET_NARROWBAND,         /**< one or two adjacent channels */
    DET_WIDEBAND,           /**< three or more adjacent channels */
    DET_UNUSUAL,            /**< unusual, without a clear channel pattern */
} det_kind_t;

/**
 * @brief   Description of a scan
 */
typedef struct {
    float level;            /**< 0 for usual scans, DET_ALARM_LEVEL and more for unusual ones */
    det_kind_t kind;        /**< kind of interference */
    uint8_t first;          /**< first channel of the interference */
    uint8_t last;           /**< last channel of the interference */
} det_result_t;

/**
 * @brief   Forget the learned scans and the model
 */
void det_learn_reset(void);

/**
 * @brief   Store a scan for learning
 *
 * @return  number of scans stored
 */
unsigned det_learn_add(const float occupancy[DET_CHANNELS]);

/**
 * @brief   Fit the model to the stored scans
 *
 * @return  0 on success, -1 with too few scans
 */
int det_learn_fit(void);

/**
 * @brief   Tell whether a model was fitted
 */
bool det_trained(void);

/**
 * @brief   Mean occupancy of a channel while learning, 0 to 1
 */
float det_learned_mean(unsigned channel);

/**
 * @brief   Score a scan against the model
 */
void det_score(const float occupancy[DET_CHANNELS], det_result_t *result);

/**
 * @brief   Name of a kind of interference
 */
const char *det_kind_name(det_kind_t kind);

#ifdef __cplusplus
}
#endif
