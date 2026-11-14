/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    demo_board Buttons, speaker and microphone of the demo boards
 * @{
 *
 * @file
 * @brief       Same button API on the Adafruit CLUE (A and B on GPIOs) and
 *              the Adafruit PyBadge (8 buttons behind a 74HC165 shift
 *              register)
 */

#include <stdint.h>

#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Button bits
 * @{
 */
#define BTN_A       (1U << 0)
#define BTN_B       (1U << 1)
#define BTN_UP      (1U << 2)
#define BTN_DOWN    (1U << 3)
#define BTN_LEFT    (1U << 4)
#define BTN_RIGHT   (1U << 5)
#define BTN_START   (1U << 6)
#define BTN_SELECT  (1U << 7)
/** @} */

/**
 * @brief   Button state with edges, see @ref buttons_update
 */
typedef struct {
    uint8_t held;       /**< buttons currently pressed */
    uint8_t pressed;    /**< buttons pressed since the previous update */
    uint8_t released;   /**< buttons released since the previous update */
} buttons_t;

/**
 * @brief   Initialize the button inputs
 */
void buttons_init(void);

/**
 * @brief   Read the buttons currently pressed, as a mask of BTN_* bits
 */
uint8_t buttons_read(void);

/**
 * @brief   Read the buttons and compute the edges since the last call
 */
void buttons_update(buttons_t *state);

/**
 * @brief   Enable the speaker amplifier when there is one and return the
 *          speaker pin, GPIO_UNDEF without speaker
 */
gpio_t board_speaker_init(void);

/**
 * @brief   Free the microphone pins before calling pdm_init
 *
 * Older versions of the CLUE periph_conf.h give P0.00, the PDM data pin, as
 * the SCL pin of the first I2C bus. i2c_init() leaves that TWIM enabled on
 * the pin, which then conflicts with the PDM peripheral. This disables any
 * I2C bus configured on the microphone data pin.
 */
void board_mic_prepare(void);

#ifdef __cplusplus
}
#endif

/** @} */
