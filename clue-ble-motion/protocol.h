/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Packets of the GATT service of CLUE BLE Motion
 *
 * The service and its characteristics have the UUIDs
 * c1ae01XX-5d8a-4a43-9a3c-6f5c2b1e7d90, XX given below. All the values are
 * little endian. web/protocol.js decodes the same packets.
 *
 * Plain C without RIOT, so that it runs in the host test.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_UUID_SERVICE     (0x01)  /**< the service */
#define MOTION_UUID_INFO        (0x02)  /**< read: see motion_pack_info() */
#define MOTION_UUID_MOTION      (0x03)  /**< notify: see motion_pack() */
#define MOTION_UUID_RAW         (0x04)  /**< notify: see motion_pack_raw() */
#define MOTION_UUID_CONTROL     (0x05)  /**< write: [command, argument] */

#define MOTION_VERSION          (1U)    /**< version of these packets */

#define MOTION_INFO_LEN         (6U)    /**< bytes of the info packet */
#define MOTION_LEN              (10U)   /**< bytes of a motion packet */
#define MOTION_RAW_LEN          (12U)   /**< bytes of a raw packet */

#define MOTION_FLAG_A           (1U << 0)   /**< button A held */
#define MOTION_FLAG_B           (1U << 1)   /**< button B held */
#define MOTION_FLAG_MAG         (1U << 2)   /**< the magnetometer is used */
#define MOTION_FLAG_CALIB       (1U << 3)   /**< calibration in progress */

#define MOTION_FEATURE_MAG      (1U << 0)   /**< info: a magnetometer exists */

#define MOTION_CMD_RECENTER     (1U)    /**< heading back to zero */
#define MOTION_CMD_MODE         (2U)    /**< argument 6 or 9: axes used */
#define MOTION_CMD_RATE         (3U)    /**< argument 25, 50 or 100 Hz */
#define MOTION_CMD_CALIBRATE    (4U)    /**< calibrate the gyroscope again */

/**
 * @brief   A motion packet
 */
typedef struct {
    uint8_t seq;            /**< counts the packets, wraps around */
    uint8_t flags;          /**< MOTION_FLAG_* */
    float q[4];             /**< orientation w x y z, see fusion.h */
} motion_t;

/**
 * @brief   Encode a motion packet, MOTION_LEN bytes: seq, flags, then the
 *          quaternion as 4 int16 in Q14 (16384 is 1)
 */
void motion_pack(uint8_t *buf, const motion_t *m);

/**
 * @brief   Encode a raw packet, MOTION_RAW_LEN bytes: the acceleration in
 *          mg then the rotation speed in 0.1 degree per second, 6 int16
 */
void motion_pack_raw(uint8_t *buf, const float g[3], const float dps[3]);

/**
 * @brief   Encode the info packet, MOTION_INFO_LEN bytes: version, sensor
 *          rate (uint16, Hz), notification rate (Hz), features
 *          (MOTION_FEATURE_*), axes used (6 or 9)
 */
void motion_pack_info(uint8_t *buf, unsigned sensor_hz, unsigned notify_hz,
                      bool has_mag, unsigned axes);

/**
 * @brief   Check a control write
 *
 * @return  0 when it is a known command with a valid argument
 */
int motion_parse_control(const uint8_t *buf, size_t len, uint8_t *cmd, uint8_t *arg);

#ifdef __cplusplus
}
#endif
