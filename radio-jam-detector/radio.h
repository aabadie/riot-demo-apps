/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Channel scan and test transmitter on the IEEE 802.15.4 Radio
 *              HAL of RIOT
 *
 * The radio is used directly, without network stack: AT86RF2xx (IoT-LAB M3,
 * SAMR21) or the nRF52840 radio.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Frames received while scanning, valid and with a bad CRC
 */
typedef struct {
    unsigned frames;        /**< valid frames */
    unsigned crc_errors;    /**< frames with a bad CRC */
} radio_rx_count_t;

/**
 * @brief   Start the radio
 *
 * @return  0 on success, negative errno otherwise
 */
int radio_init(void);

/**
 * @brief   Tune to a channel and listen
 *
 * @return  0 on success, negative errno otherwise
 */
int radio_set_channel(uint8_t channel, int8_t tx_power);

/**
 * @brief   Clear channel assessment with an energy threshold
 *
 * @return  1 when the energy is above @p threshold_dbm, 0 when below,
 *          negative errno on error
 */
int radio_busy(int8_t threshold_dbm);

/**
 * @brief   Frames received since the last call, they are dropped
 */
void radio_rx_count(radio_rx_count_t *count);

/**
 * @brief   Send a broadcast test frame of the largest size, without
 *          listening first, and wait for its end
 *
 * @return  0 on success, negative errno otherwise
 */
int radio_send_test_frame(void);

/**
 * @brief   Time on air of a test frame, in microseconds
 */
uint32_t radio_test_frame_us(void);

#ifdef __cplusplus
}
#endif
