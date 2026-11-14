/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Line protocol between tools/send_digits.py and the board
 *
 * One line per message, in ASCII:
 *
 *     PC to board:   D <label> <1568 hex digits>    classify an MNIST image
 *                    N <label> <1568 hex digits>    normalize, then classify
 *                    ?                              ask for the banner
 *     board to PC:   I digit-oracle <version>       banner
 *                    P <digit> <us> <p0> ... <p9>   result, probabilities in
 *                                                   percent
 *                    B <1568 hex digits>            digit drawn on the board
 *                    E <message>                    error
 *
 * The label is the expected digit, or '-' when unknown.
 */

#include <stddef.h>
#include <stdint.h>

#include "digits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_VERSION   "1"     /**< protocol version in the banner */
#define PROTO_LINE_MAX  (DIGITS_PIXELS * 2 + 8)     /**< longest line */

/**
 * @brief   Parsed request
 */
typedef struct {
    char cmd;                       /**< 'D', 'N' or '?' */
    int label;                      /**< expected digit, -1 when unknown */
    uint8_t image[DIGITS_PIXELS];   /**< image of D and N */
} proto_request_t;

/**
 * @brief   Parse a line, without its end of line
 *
 * @return  0 on success, -1 on a malformed line
 */
int proto_parse(const char *line, proto_request_t *req);

/**
 * @brief   Format a result line, with its end of line
 *
 * @return  length of the line
 */
size_t proto_format_result(char *buf, size_t len, int digit, unsigned us,
                           const float proba[DIGITS_CLASSES]);

/**
 * @brief   Format an image as a B line, with its end of line
 *
 * @p buf must hold at least PROTO_LINE_MAX bytes.
 *
 * @return  length of the line
 */
size_t proto_format_image(char *buf, const uint8_t *image);

#ifdef __cplusplus
}
#endif
