/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: QR codes on the thermal printer
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adafruit_thermal.h"
#include "qrcodegen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Modules of the quiet zone around the code, on each side
 */
#define QR_QUIET            (4U)

/**
 * @brief   Smallest module, in dots: 0.5 mm, read by any phone
 */
#define QR_SCALE_MIN        (4U)

/**
 * @brief   Largest module, in dots: 1 mm
 */
#define QR_SCALE_MAX        (8U)

/**
 * @brief   Highest version, the largest code printed with modules of
 *          @ref QR_SCALE_MIN dots
 */
#define QR_VERSION_MAX      ((ADAFRUIT_THERMAL_WIDTH / QR_SCALE_MIN - 2 * QR_QUIET - 17) / 4)

/**
 * @brief   Buffer of a QR code
 */
#define QR_BUFFER_LEN       qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)

/**
 * @brief   Encode a text into a QR code
 *
 * The error correction is the low one, raised when the text still fits in
 * the same version.
 *
 * @param[in]  text     null terminated text
 * @param[out] qr       QR code, @ref QR_BUFFER_LEN bytes
 *
 * @return  true on success, false when the text does not fit
 */
bool qr_encode(const char *text, uint8_t *qr);

/**
 * @brief   Print a QR code, centered, with its quiet zone
 *
 * @param[in] dev   printer
 * @param[in] qr    QR code
 */
void qr_print(adafruit_thermal_t *dev, const uint8_t *qr);

/**
 * @brief   Print the text of a QR code below it, when it is short plain
 *          ASCII, else its length
 *
 * @param[in] dev   printer
 * @param[in] text  null terminated text
 */
void qr_print_caption(adafruit_thermal_t *dev, const char *text);

#ifdef __cplusplus
}
#endif
