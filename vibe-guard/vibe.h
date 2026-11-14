/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Vibration anomaly detection, plain C without RIOT dependencies
 *              so that it can be tested on the host
 *
 * The acceleration is cut in windows of VIBE_WINDOW samples. Each window
 * gives the power of the vibration in VIBE_BANDS frequency bands, summed
 * over the three axes, in dB. A Gaussian mixture is fitted on the windows
 * of the learning phase, on the device, then emlearn scores new windows
 * against it.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIBE_RATE_HZ        (200)   /**< sample rate */
#define VIBE_WINDOW         (128)   /**< samples per window, 0.64 s */
#define VIBE_HOP            (64)    /**< samples between two windows */
#define VIBE_BANDS          (8)     /**< features per window */
#define VIBE_COMPONENTS     (3)     /**< Gaussian components */
#define VIBE_TRAIN_MAX      (160)   /**< windows kept for learning */
#define VIBE_ALARM_LEVEL    (3.0f)  /**< level above which a window is odd */

/**
 * @brief   Lower edges of the frequency bands in Hz, plus the upper edge of
 *          the last band
 */
extern const float vibe_band_edges[VIBE_BANDS + 1];

/**
 * @brief   Initialize the FFT tables
 */
void vibe_init(void);

/**
 * @brief   Compute the features of a window
 *
 * @param[in]  window   VIBE_WINDOW samples of x, y and z, in g
 * @param[out] features band powers in dB
 */
void vibe_features(const float window[VIBE_WINDOW][3],
                   float features[VIBE_BANDS]);

/**
 * @brief   Forget the learned windows and the model
 */
void vibe_learn_reset(void);

/**
 * @brief   Add the features of a normal window
 *
 * @return  number of windows learned so far
 */
unsigned vibe_learn_add(const float features[VIBE_BANDS]);

/**
 * @brief   Fit the model on the learned windows
 *
 * @return  0 on success, -1 with too few windows
 */
int vibe_learn_fit(void);

/**
 * @brief   Whether a model has been fitted
 */
bool vibe_trained(void);

/**
 * @brief   Anomaly level of a window
 *
 * 0 to 1 is the spread of the learned windows, VIBE_ALARM_LEVEL and above
 * is unusual.
 *
 * @param[in]  features     features of the window
 * @param[out] log_lik      log-likelihood of the window, can be NULL
 */
float vibe_level(const float features[VIBE_BANDS], float *log_lik);

/**
 * @brief   Average features of the learned windows, for display
 */
const float *vibe_learned_mean(void);

#ifdef __cplusplus
}
#endif
