/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: the parts of the application
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Longest text, in bytes: the longest value of a BLE attribute
 *
 * A QR code of version 17 (85 modules, 4 dots each on the 384 dots of the
 * printer) holds 644 bytes with the low error correction: any text fits.
 */
#define APP_TEXT_MAX        (512U)

/**
 * @name    State of the printer, as sent over BLE
 * @{
 */
#define APP_STATE_READY     (0U)    /**< waiting for a text */
#define APP_STATE_PRINTING  (1U)    /**< printing a text */
/** @} */

/**
 * @brief   Queue a text for the printer, from any thread
 *
 * @param[in] text  text, not null terminated
 * @param[in] len   length of @p text
 *
 * @return  0 when queued
 * @return  -EINVAL for an empty text or one with a null byte
 * @return  -EMSGSIZE for a text longer than @ref APP_TEXT_MAX
 * @return  -EBUSY while the printer prints another text
 */
int app_submit(const char *text, size_t len);

/**
 * @brief   Start the GATT service and the advertising
 */
void app_ble_init(void);

/**
 * @brief   Tell the BLE client the state of the printer
 *
 * @param[in] state     APP_STATE_*
 */
void app_ble_state(uint8_t state);

/**
 * @brief   Start the screen
 */
void app_screen_init(void);

/**
 * @brief   Show a QR code and a status line on the screen
 *
 * @param[in] qr        QR code of qrcodegen, NULL for none
 * @param[in] status    status line
 */
void app_screen_show(const uint8_t *qr, const char *status);

#ifdef __cplusplus
}
#endif
