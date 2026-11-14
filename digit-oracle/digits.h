/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Handwritten digit recognition, plain C without RIOT
 *              dependencies so that it can be tested on the host
 *
 * Images are 28x28 bytes, row by row, 0 for the background and 255 for the
 * ink, like the MNIST digits.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITS_SIZE     (28)                            /**< image side */
#define DIGITS_PIXELS   (DIGITS_SIZE * DIGITS_SIZE)     /**< image size */
#define DIGITS_CLASSES  (10)                            /**< digits 0 to 9 */

/**
 * @brief   Normalize an image the way the MNIST digits were prepared
 *
 * The digit is scaled to fit a 20x20 box, keeping its aspect ratio, then
 * moved so that its center of mass is the center of the image.
 *
 * @param[in]  in   image to normalize
 * @param[out] out  normalized image, can't be @p in
 *
 * @return  0 on success
 * @return  -1 when @p in is empty
 */
int digits_normalize(const uint8_t *in, uint8_t *out);

/**
 * @brief   Classify a normalized image
 *
 * @param[in]  image    image to classify
 * @param[out] proba    probability of each digit
 *
 * @return  the most likely digit
 * @return  -1 on error
 */
int digits_classify(const uint8_t *image, float proba[DIGITS_CLASSES]);

#ifdef __cplusplus
}
#endif
