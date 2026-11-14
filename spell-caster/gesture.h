/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Gesture features and classification, plain C without RIOT
 *              dependencies: tools/train.py calls the same code through
 *              ctypes, so that the features of the training are exactly the
 *              ones of the board
 *
 * A gesture is a sequence of samples of 6 channels: the acceleration in g
 * and the rotation speed in degrees per second, both in the axes of the
 * sensor. Boards without gyroscope give 0 for the last 3 channels.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GESTURE_RATE_HZ         (100)   /**< sample rate */
#define GESTURE_MIN_SAMPLES     (15)    /**< shorter gestures are ignored */
#define GESTURE_MAX_SAMPLES     (300)   /**< 3 seconds */
#define GESTURE_CHANNELS        (6)     /**< ax ay az gx gy gz */
#define GESTURE_FEAT_CHANNELS   (GESTURE_CHANNELS + 1)  /**< plus |a| */
#define GESTURE_FEAT_PER_CH     (10)    /**< features per channel */
/** Number of features */
#define GESTURE_FEATURES        (GESTURE_FEAT_CHANNELS * GESTURE_FEAT_PER_CH + 1)
#define GESTURE_CLASSES         (5)     /**< gestures known by the model */

/**
 * @brief   Names of the gestures, in the order of the classes of the model,
 *          the same as LABELS in tools/gestures.py
 */
#define GESTURE_NAMES   { "CIRCLE", "ZIGZAG", "FLICK", "SHAKE", "THRUST" }

/**
 * @brief   Compute the features of a gesture
 *
 * @param[in]  samples  @p n samples of GESTURE_CHANNELS floats
 * @param[in]  n        number of samples
 * @param[out] features GESTURE_FEATURES integers
 *
 * @return  0 on success, -1 when the gesture is too short
 */
int gesture_features(const float *samples, unsigned n, int16_t *features);

/**
 * @brief   Classify a gesture from its features
 *
 * @param[in]  features GESTURE_FEATURES integers
 * @param[out] proba    probability of each gesture
 *
 * @return  the most likely gesture, -1 on error
 */
int gesture_classify(const int16_t *features, float *proba);

#ifdef __cplusplus
}
#endif
