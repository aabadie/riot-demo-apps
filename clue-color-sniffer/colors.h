/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Colors taught on the device and recognized with an emlearn
 *              Gaussian naive Bayes model, plain C without RIOT
 *              dependencies so that it can be tested on the host
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLORS_SLOTS        (6)     /**< colors that can be taught */
#define COLORS_FEATURES     (3)     /**< red and green share, brightness */
#define COLORS_MIN_SAMPLES  (3)     /**< samples before a slot is used */
#define COLORS_MAX_Z        (3.5f)  /**< mean distance in standard deviations
                                         above which a color is unknown */

/**
 * @brief   Features of a reading of the color sensor
 *
 * The share of red and green in red + green + blue, which do not depend on
 * how far the object is, and the brightness, which does a bit.
 */
void colors_features(uint16_t red, uint16_t green, uint16_t blue,
                     uint16_t clear, float features[COLORS_FEATURES]);

/**
 * @brief   Forget all the slots
 */
void colors_reset(void);

/**
 * @brief   Forget one slot
 */
void colors_clear(unsigned slot);

/**
 * @brief   Teach a sample of a slot
 */
void colors_teach(unsigned slot, const float features[COLORS_FEATURES]);

/**
 * @brief   Number of samples taught to a slot
 */
unsigned colors_count(unsigned slot);

/**
 * @brief   Recognize a color
 *
 * @param[in]  features features of the reading
 * @param[out] z        mean distance to the slot, in standard deviations
 *
 * @return  the slot, -1 when nothing matches or nothing has been taught
 */
int colors_predict(const float features[COLORS_FEATURES], float *z);

#ifdef __cplusplus
}
#endif
