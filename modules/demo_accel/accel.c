/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     demo_accel
 * @{
 *
 * @file
 * @brief       Accelerometer and gyroscope of the demo boards
 *
 * @}
 */

#include <stddef.h>

#include "kernel_defines.h"
#include "demo_accel.h"

#if IS_USED(MODULE_LIS3DH)
#  include "lis3dh.h"
#  include "lis3dh_params.h"
#elif IS_USED(MODULE_LSM6DSXX)
#  include "lsm6dsxx.h"
#  include "lsm6dsxx_params.h"
#endif

/* Board defaults of the screen axes, unverified on hardware */
#ifndef ACCEL_SWAP_XY
#  define ACCEL_SWAP_XY     (0)
#endif
#ifndef ACCEL_SIGN_X
#  define ACCEL_SIGN_X      (1)
#endif
#ifndef ACCEL_SIGN_Y
#  define ACCEL_SIGN_Y      (1)
#endif

#if IS_USED(MODULE_LIS3DH)

static bool _ok;
static lis3dh_t _dev;

static const struct {
    unsigned hz;
    uint8_t odr;
} _rates[] = {
    { 10, LIS3DH_ODR_10Hz }, { 25, LIS3DH_ODR_25Hz }, { 50, LIS3DH_ODR_50Hz },
    { 100, LIS3DH_ODR_100Hz }, { 200, LIS3DH_ODR_200Hz },
    { 400, LIS3DH_ODR_400Hz },
};

unsigned accel_init(unsigned rate_hz)
{
    unsigned i = 0;

    while (i < ARRAY_SIZE(_rates) - 1 && _rates[i].hz < rate_hz) {
        i++;
    }
    _ok = lis3dh_init(&_dev, &lis3dh_params[0]) == 0 &&
          lis3dh_set_odr(&_dev, _rates[i].odr) == 0 &&
          lis3dh_set_scale(&_dev, 4) == 0;
    return _ok ? _rates[i].hz : 0;
}

bool accel_read(float g[3])
{
    lis3dh_data_t data;

    if (!_ok || lis3dh_read_xyz(&_dev, &data) != 0) {
        return false;
    }
    g[0] = data.acc_x / 1000.0f;
    g[1] = data.acc_y / 1000.0f;
    g[2] = data.acc_z / 1000.0f;
    return true;
}

bool accel_has_gyro(void)
{
    return false;
}

bool gyro_read(float dps[3])
{
    (void)dps;
    return false;
}

#elif IS_USED(MODULE_LSM6DSXX)

static bool _ok;
static lsm6dsxx_t _dev;

static const struct {
    unsigned hz;
    uint8_t odr;
} _rates[] = {
    { 13, LSM6DSXX_DATA_RATE_12_5HZ }, { 26, LSM6DSXX_DATA_RATE_26HZ },
    { 52, LSM6DSXX_DATA_RATE_52HZ }, { 104, LSM6DSXX_DATA_RATE_104HZ },
    { 208, LSM6DSXX_DATA_RATE_208HZ }, { 416, LSM6DSXX_DATA_RATE_416HZ },
    { 833, LSM6DSXX_DATA_RATE_833HZ },
};

unsigned accel_init(unsigned rate_hz)
{
    unsigned i = 0;
    lsm6dsxx_params_t params = lsm6dsxx_params[0];

    while (i < ARRAY_SIZE(_rates) - 1 && _rates[i].hz < rate_hz) {
        i++;
    }
    params.acc_odr = _rates[i].odr;
    params.gyro_odr = _rates[i].odr;
    params.acc_fs = LSM6DSXX_ACC_FS_4G;
    params.gyro_fs = LSM6DSXX_GYRO_FS_1000DPS;
    _ok = lsm6dsxx_init(&_dev, &params) == LSM6DSXX_OK;
    return _ok ? _rates[i].hz : 0;
}

bool accel_read(float g[3])
{
    lsm6dsxx_3d_data_t data;

    if (!_ok || lsm6dsxx_read_acc(&_dev, &data) != LSM6DSXX_OK) {
        return false;
    }
    g[0] = data.x / 1000.0f;
    g[1] = data.y / 1000.0f;
    g[2] = data.z / 1000.0f;
    return true;
}

bool accel_has_gyro(void)
{
    return _ok;
}

bool gyro_read(float dps[3])
{
    lsm6dsxx_3d_data_t data;

    if (!_ok || lsm6dsxx_read_gyro(&_dev, &data) != LSM6DSXX_OK) {
        return false;
    }
    /* the driver gives tenths of degrees per second */
    dps[0] = data.x / 10.0f;
    dps[1] = data.y / 10.0f;
    dps[2] = data.z / 10.0f;
    return true;
}

#else

unsigned accel_init(unsigned rate_hz)
{
    (void)rate_hz;
    return 0;
}

bool accel_read(float g[3])
{
    (void)g;
    return false;
}

bool accel_has_gyro(void)
{
    return false;
}

bool gyro_read(float dps[3])
{
    (void)dps;
    return false;
}

#endif

bool accel_tilt(float *x, float *y)
{
    float g[3] = { 0 };

    if (!accel_read(g)) {
        *x = 0;
        *y = 0;
        return false;
    }
    *x = ACCEL_SIGN_X * (ACCEL_SWAP_XY ? g[1] : g[0]);
    *y = ACCEL_SIGN_Y * (ACCEL_SWAP_XY ? g[0] : g[1]);
    return true;
}
