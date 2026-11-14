/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Packets of the GATT service of the dashboard, plain C without
 *              RIOT dependencies so that it can be tested on the host
 *
 * All the values are little endian. web/protocol.js decodes them on the
 * phone.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   128-bit UUIDs of the service and its characteristics,
 *          c1ae00XX-5d8a-4a43-9a3c-6f5c2b1e7d90, XX given here
 * @{
 */
#define DASH_UUID_SERVICE       (0x01)
#define DASH_UUID_SENSORS       (0x02)  /**< read, notify: sensors packet */
#define DASH_UUID_STATUS        (0x03)  /**< read, notify: status packet */
#define DASH_UUID_TEXT          (0x04)  /**< write: text for the screen */
#define DASH_UUID_TEACH         (0x05)  /**< write: teach command */
#define DASH_UUID_TONE          (0x06)  /**< write: note to play */
/** @} */

#define DASH_SENSORS_LEN        (20)    /**< fits the default ATT MTU */
#define DASH_STATUS_LEN         (11)
#define DASH_TEXT_MAX           (60)
#define DASH_SLOTS              (6)

#define DASH_INVALID_S16        (-32768)
#define DASH_INVALID_U16        (0xffff)

/**
 * @name    Status of the color recognition
 * @{
 */
#define DASH_MATCH_NONE         (-1)    /**< no taught color matches */
#define DASH_MATCH_FAR          (-2)    /**< no object near the sensor */
/** @} */

/**
 * @name    Teach commands: command byte, then slot byte
 * @{
 */
#define DASH_TEACH_SAMPLES      (1)     /**< teach samples to the slot */
#define DASH_TEACH_CLEAR        (2)     /**< forget the slot */
#define DASH_TEACH_CLEAR_ALL    (3)     /**< forget all the slots */
/** @} */

/**
 * @brief   Sensor values, NAN or false when missing
 */
typedef struct {
    bool has_accel;
    float accel_g[3];
    float temp_c;
    float hum;
    float press_hpa;
    float lux;
    bool has_rgb;
    uint16_t rgb[3];            /**< raw counts of the color sensor */
} dash_sensors_t;

/**
 * @brief   State of the color recognition and of the board
 */
typedef struct {
    int8_t match;               /**< slot, or DASH_MATCH_* */
    float distance;             /**< in standard deviations */
    int8_t rssi;                /**< of the connection in dBm, 0 unknown */
    uint16_t proximity;         /**< raw proximity reading */
    unsigned counts[DASH_SLOTS];/**< samples taught to each slot */
    uint8_t buttons;            /**< bit 0: A, bit 1: B */
} dash_status_t;

/**
 * @brief   Encode the sensors packet, DASH_SENSORS_LEN bytes
 */
void dash_pack_sensors(uint8_t buf[DASH_SENSORS_LEN], const dash_sensors_t *s);

/**
 * @brief   Encode the status packet, DASH_STATUS_LEN bytes
 */
void dash_pack_status(uint8_t buf[DASH_STATUS_LEN], const dash_status_t *s);

/**
 * @brief   Turn a text written by the phone into what the screen can draw:
 *          upper case, printable ASCII, at most DASH_TEXT_MAX characters
 *
 * @param[out] out  DASH_TEXT_MAX + 1 bytes
 */
void dash_clean_text(char *out, const uint8_t *in, size_t len);

#ifdef __cplusplus
}
#endif
