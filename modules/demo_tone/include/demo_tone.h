/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    demo_tone Square wave tones and melodies on a GPIO
 * @{
 *
 * @file
 * @brief       Plays a looping melody and short sound effects on a speaker
 *              or buzzer driven by a GPIO. Effects pause the melody.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Note frequencies in Hz
 * @{
 */
#define NOTE_REST   (0)
#define NOTE_G3     (196)
#define NOTE_A3     (220)
#define NOTE_B3     (247)
#define NOTE_C4     (262)
#define NOTE_D4     (294)
#define NOTE_E4     (330)
#define NOTE_F4     (349)
#define NOTE_G4     (392)
#define NOTE_A4     (440)
#define NOTE_B4     (494)
#define NOTE_C5     (523)
#define NOTE_D5     (587)
#define NOTE_E5     (659)
#define NOTE_F5     (698)
#define NOTE_G5     (784)
#define NOTE_A5     (880)
#define NOTE_B5     (988)
#define NOTE_C6     (1047)
#define NOTE_D6     (1175)
#define NOTE_E6     (1319)
#define NOTE_G6     (1568)
#define NOTE_A6     (1760)
/** @} */

/**
 * @brief   One note of a melody
 */
typedef struct {
    uint16_t freq;      /**< frequency in Hz, NOTE_REST for silence */
    uint16_t ms;        /**< duration in milliseconds */
} tone_note_t;

/**
 * @brief   Initialize the tone output on a GPIO
 */
void tone_init(gpio_t pin);

/**
 * @brief   Start playing a melody, replacing the current one
 *
 * @param[in] notes     notes, must stay valid while playing
 * @param[in] len       number of notes
 * @param[in] loop      restart from the first note at the end
 */
void tone_play(const tone_note_t *notes, size_t len, bool loop);

/**
 * @brief   Stop the melody
 */
void tone_stop(void);

/**
 * @brief   Play a sound effect, the melody resumes when it ends
 *
 * @param[in] notes     notes, must stay valid while playing
 * @param[in] len       number of notes
 */
void tone_effect(const tone_note_t *notes, size_t len);

/**
 * @brief   Play a single beep as an effect
 */
void tone_beep(uint16_t freq, uint16_t ms);

/**
 * @brief   Mute or unmute all sounds
 */
void tone_mute(bool mute);

/**
 * @brief   Tell whether the sounds are muted
 */
bool tone_is_muted(void);

#ifdef __cplusplus
}
#endif

/** @} */
