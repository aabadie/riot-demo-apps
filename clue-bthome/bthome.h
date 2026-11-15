/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       BTHome v2 advertising data and button events, plain C without
 *              RIOT dependencies so that it can be tested on the host
 *
 * See https://bthome.io/format/ for the format.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTHOME_UUID             (0xfcd2U)   /**< 16-bit service UUID */
#define BTHOME_BUTTONS          (2)         /**< buttons sent */

/**
 * @name    Button events of BTHome
 * @{
 */
#define BTHOME_EV_NONE          (0x00)
#define BTHOME_EV_PRESS         (0x01)
#define BTHOME_EV_DOUBLE        (0x02)
#define BTHOME_EV_TRIPLE        (0x03)
#define BTHOME_EV_LONG          (0x04)
/** @} */

/**
 * @brief   Values of an advertisement, the ones not valid are left out
 */
typedef struct {
    uint8_t packet_id;          /**< changes when the values change */
    bool has_temp;
    float temp_c;               /**< temperature in degrees Celsius */
    bool has_press;
    float press_hpa;            /**< pressure in hPa */
    bool has_lux;
    float lux;                  /**< illuminance in lux */
    bool has_motion;
    bool moving;                /**< the board moves */
    bool vibration;             /**< unusual vibration, once learned */
    bool has_hum;
    float hum;                  /**< relative humidity in percent */
    uint8_t button[BTHOME_BUTTONS]; /**< BTHOME_EV_* of each button */
} bthome_data_t;

/**
 * @brief   Encode the service data of an advertisement: the UUID, the
 *          device information byte and the objects, sorted by id
 *
 * At most 23 bytes of objects, to fit in a legacy advertisement with the
 * flags.
 *
 * @return  number of bytes written, 0 when @p size is too small
 */
size_t bthome_encode(uint8_t *buf, size_t size, const bthome_data_t *data);

/**
 * @brief   Press, double press, triple press and long press of a button
 */
typedef struct {
    bool held;                  /**< state at the previous update */
    uint8_t presses;            /**< presses of the current sequence */
    uint32_t since;             /**< time of the last change, in ms */
    bool long_sent;             /**< long press already reported */
} bthome_button_t;

#define BTHOME_LONG_MS          (800U)  /**< hold time of a long press */
#define BTHOME_GAP_MS           (350U)  /**< max time between presses of a
                                             double or triple press */

/**
 * @brief   Update a button with its current state
 *
 * @return  BTHOME_EV_* when a sequence of presses is over, else
 *          BTHOME_EV_NONE
 */
uint8_t bthome_button_update(bthome_button_t *btn, bool held, uint32_t now_ms);

/**
 * @brief   When to send a new packet id
 *
 * A receiver takes the events of each new packet id as new events, so an
 * event goes out with one packet id only, repeated by the advertising for
 * BTHOME_EVENT_MS. The other changes wait until then.
 */
typedef struct {
    bool on_air;                /**< an event is advertised */
    bool pending;               /**< changes wait for the next packet id */
    uint32_t until;             /**< end of the event, in ms */
} bthome_sched_t;

#define BTHOME_EVENT_MS         (1500U) /**< time an event is advertised */

/**
 * @brief   Take the new button events and the other changes
 *
 * @param[inout] sched      state
 * @param[inout] data       data advertised, its buttons are set to @p events
 *                          or cleared when the event is over
 * @param[in]    events     BTHOME_EV_* of each button for this update
 * @param[in]    changed    other values of @p data changed
 * @param[in]    now_ms     current time
 *
 * @return  true when @p data must be advertised with a new packet id
 */
bool bthome_sched_update(bthome_sched_t *sched, bthome_data_t *data,
                         const uint8_t events[BTHOME_BUTTONS], bool changed,
                         uint32_t now_ms);

#ifdef __cplusplus
}
#endif
