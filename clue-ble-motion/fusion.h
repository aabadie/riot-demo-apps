/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Orientation of the board from its gyroscope, accelerometer
 *              and, optionally, magnetometer: the filter of Madgwick
 *
 * The orientation is a unit quaternion q = (w, x, y, z) that turns a vector
 * of the sensor frame into the earth frame, z up: v_earth = q v q*. The
 * gyroscope is integrated, and a gradient descent step pulls the estimate
 * towards the measured gravity (and the magnetic north). Without a
 * magnetometer, the heading drifts slowly; fusion_recenter() sets it back.
 *
 * Plain C without RIOT, so that it runs in the host test.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FUSION_BETA         (0.08f)     /**< gain of the correction */
#define FUSION_BETA_START   (2.0f)      /**< gain while it converges */
#define FUSION_START_S      (1.5f)      /**< time of the start gain */
#define FUSION_CALIB_S      (2.0f)      /**< time of the gyroscope calibration */
#define FUSION_STILL_DPS    (4.0f)      /**< max rotation speed while still */

/**
 * @brief   State of the filter
 */
typedef struct {
    float q[4];             /**< orientation, w x y z */
    float time_s;           /**< time since the start */
    float ref[4];           /**< heading set by fusion_recenter() */
} fusion_t;

/**
 * @brief   Gyroscope bias, averaged while the board is still
 */
typedef struct {
    float sum[3];           /**< sum of the samples */
    float min[3];           /**< extremes of the samples */
    float max[3];           /**< extremes of the samples */
    float time_s;           /**< time averaged */
    unsigned count;         /**< samples averaged */
    float bias[3];          /**< result, in degrees per second */
    bool running;           /**< calibration in progress */
} fusion_bias_t;

/**
 * @brief   Start from the identity, with a high gain to converge quickly
 */
void fusion_init(fusion_t *f);

/**
 * @brief   Update the orientation with a new sample
 *
 * @param[inout] f      state
 * @param[in]    dps    rotation speed in degrees per second, bias removed
 * @param[in]    g      acceleration, any unit
 * @param[in]    mag    magnetic field, any unit, NULL without magnetometer
 * @param[in]    dt     time since the last sample, in seconds
 */
void fusion_update(fusion_t *f, const float dps[3], const float g[3],
                   const float *mag, float dt);

/**
 * @brief   The orientation relative to the heading set by fusion_recenter()
 */
void fusion_relative(const fusion_t *f, float q[4]);

/**
 * @brief   Make the current heading the reference, the tilt is kept
 */
void fusion_recenter(fusion_t *f);

/**
 * @brief   Roll, pitch and yaw of a quaternion, in degrees
 */
void fusion_euler(const float q[4], float *roll, float *pitch, float *yaw);

/**
 * @brief   Product of two quaternions, out = a b
 */
void fusion_mul(const float a[4], const float b[4], float out[4]);

/**
 * @brief   Start averaging the gyroscope bias
 */
void fusion_bias_start(fusion_bias_t *b);

/**
 * @brief   Add a sample to the bias calibration
 *
 * The calibration starts over when the board moves.
 *
 * @return  true when the calibration just finished
 */
bool fusion_bias_add(fusion_bias_t *b, const float dps[3], float dt);

#ifdef __cplusplus
}
#endif
