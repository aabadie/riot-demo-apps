/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     demo_board
 * @{
 *
 * @file
 * @brief       Buttons, speaker and microphone of the demo boards
 *
 * @}
 */

#include "board.h"
#include "cpu.h"
#include "kernel_defines.h"
#include "periph/i2c.h"
#include "demo_board.h"

#if IS_USED(MODULE_SN74HC165)
/* PyBadge: the buttons are behind a 74HC165 shift register */
#  include "sn74hc165.h"
#  include "sn74hc165_params.h"
#  define HAVE_SHIFT_BUTTONS    1
static sn74hc165_t _shift;
#endif

void buttons_init(void)
{
#ifdef HAVE_SHIFT_BUTTONS
    sn74hc165_init(&_shift, &sn74hc165_params[0]);
#else
#  ifdef BTN0_PIN
    gpio_init(BTN0_PIN, BTN0_MODE);
#  endif
#  ifdef BTN1_PIN
    gpio_init(BTN1_PIN, BTN1_MODE);
#  endif
#endif
}

uint8_t buttons_read(void)
{
    uint8_t buttons = 0;

#ifdef HAVE_SHIFT_BUTTONS
    uint32_t raw = sn74hc165_read(&_shift);
    static const struct { uint8_t mask; uint8_t button; } map[] = {
        { BUTTON_MASK_A, BTN_A }, { BUTTON_MASK_B, BTN_B },
        { BUTTON_MASK_UP, BTN_UP }, { BUTTON_MASK_DOWN, BTN_DOWN },
        { BUTTON_MASK_LEFT, BTN_LEFT }, { BUTTON_MASK_RIGHT, BTN_RIGHT },
        { BUTTON_MASK_START, BTN_START }, { BUTTON_MASK_SELECT, BTN_SELECT },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (raw & map[i].mask) {
            buttons |= map[i].button;
        }
    }
#else
    /* the CLUE buttons pull the pins low when pressed */
#  ifdef BTN0_PIN
    if (!gpio_read(BTN0_PIN)) {
        buttons |= BTN_A;
    }
#  endif
#  ifdef BTN1_PIN
    if (!gpio_read(BTN1_PIN)) {
        buttons |= BTN_B;
    }
#  endif
#endif
    return buttons;
}

void buttons_update(buttons_t *state)
{
    uint8_t now = buttons_read();
    state->pressed = now & ~state->held;
    state->released = state->held & ~now;
    state->held = now;
}

gpio_t board_speaker_init(void)
{
#if defined(SPEAKER_ENABLE_PIN)
    gpio_init(SPEAKER_ENABLE_PIN, GPIO_OUT);
    gpio_set(SPEAKER_ENABLE_PIN);
#endif
#if defined(SPEAKER_PIN)
    return SPEAKER_PIN;
#elif defined(SPKR_PIN)
    return SPKR_PIN;
#else
    return GPIO_UNDEF;
#endif
}

void board_mic_prepare(void)
{
#if defined(BOARD_ADAFRUIT_CLUE) && defined(PDM_DIN_PIN)
    /* only needed with the old board configuration */
    for (unsigned i = 0; i < I2C_NUMOF; i++) {
        if (i2c_config[i].scl == PDM_DIN_PIN || i2c_config[i].sda == PDM_DIN_PIN) {
            i2c_config[i].dev->ENABLE = TWIM_ENABLE_ENABLE_Disabled;
            i2c_config[i].dev->PSEL.SCL = TWIM_PSEL_SCL_CONNECT_Msk;
            i2c_config[i].dev->PSEL.SDA = TWIM_PSEL_SDA_CONNECT_Msk;
        }
    }
#endif
}
