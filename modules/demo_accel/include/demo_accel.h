/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    demo_accel Accelerometer and gyroscope of the demo boards
 * @{
 *
 * @file
 * @brief       Same motion sensor API on the Adafruit PyBadge (LIS3DH,
 *              accelerometer only) and the Adafruit CLUE (LSM6DS33 or
 *              LSM6DS3TR-C, accelerometer and gyroscope)
 *
 * Values are given in the axes of the sensor. @ref accel_tilt maps them to
 * the axes of the screen, with board defaults that can be changed with the
 * ACCEL_SWAP_XY, ACCEL_SIGN_X and ACCEL_SIGN_Y macros.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Initialize the motion sensor
 *
 * @param[in] rate_hz   wanted output data rate, the next supported rate at
 *                      or above it is used
 *
 * @return  the output data rate used, in Hz
 * @return  0 when there is no motion sensor
 */
unsigned accel_init(unsigned rate_hz);

/**
 * @brief   Read the acceleration in g, in the axes of the sensor
 *
 * @return  false when there is no sensor or the read failed
 */
bool accel_read(float g[3]);

/**
 * @brief   Read the tilt in the axes of the screen, in g
 *
 * @p x is positive when the right side of the screen goes down, @p y when
 * the bottom side goes down.
 */
bool accel_tilt(float *x, float *y);

/**
 * @brief   Whether the board has a gyroscope
 */
bool accel_has_gyro(void);

/**
 * @brief   Read the rotation speed in degrees per second
 *
 * @return  false when there is no gyroscope or the read failed
 */
bool gyro_read(float dps[3]);

#ifdef __cplusplus
}
#endif

/** @} */
