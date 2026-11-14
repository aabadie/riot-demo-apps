/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Vocabulary of the voice keyboard and USB HID key codes
 *
 * Plain C without RIOT dependencies, so it can be tested on the host.
 *
 * The words are the NATO alphabet, the digits and four commands. A USB
 * keyboard sends key positions, not characters: the computer turns them
 * into characters with its own layout. Build with KEYS_AZERTY for a
 * computer set to a French layout.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYS_NUM_WORDS      (40U)       /**< size of the vocabulary */

#define KEYS_MOD_SHIFT      (0x02U)     /**< left shift modifier bit */

/* special characters of the commands */
#define KEYS_CHAR_ENTER     ('\n')      /**< ENTER */
#define KEYS_CHAR_DELETE    ('\b')      /**< DELETE, the backspace key */
#define KEYS_CHAR_SHIFT     ('\x0e')    /**< SHIFT: next letter upper case */

/**
 * @brief   A word of the vocabulary
 */
typedef struct {
    const char *name;       /**< word to say */
    const char *label;      /**< short label on the screen */
    char ch;                /**< character typed */
} keys_word_t;

/**
 * @brief   The vocabulary, KEYS_NUM_WORDS words
 */
extern const keys_word_t keys_words[KEYS_NUM_WORDS];

/**
 * @brief   USB HID usage and modifiers that type a character
 *
 * @param[in]  ch       a letter, a digit, space, KEYS_CHAR_ENTER or
 *                      KEYS_CHAR_DELETE
 * @param[out] mod      modifier bits
 * @param[out] usage    key usage of the keyboard page
 *
 * @return  true when the character can be typed
 */
bool keys_lookup(char ch, uint8_t *mod, uint8_t *usage);

#ifdef __cplusplus
}
#endif
