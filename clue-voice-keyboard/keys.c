/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Vocabulary of the voice keyboard and USB HID key codes
 */

#include "keys.h"

/* key usages of the USB HID keyboard page */
#define USAGE_A         (0x04U)
#define USAGE_Q         (0x14U)
#define USAGE_W         (0x1aU)
#define USAGE_Z         (0x1dU)
#define USAGE_1         (0x1eU)
#define USAGE_0         (0x27U)
#define USAGE_ENTER     (0x28U)
#define USAGE_BACKSPACE (0x2aU)
#define USAGE_SPACE     (0x2cU)
#define USAGE_SEMICOLON (0x33U)

const keys_word_t keys_words[KEYS_NUM_WORDS] = {
    { "ALFA", "A", 'a' }, { "BRAVO", "B", 'b' }, { "CHARLIE", "C", 'c' },
    { "DELTA", "D", 'd' }, { "ECHO", "E", 'e' }, { "FOXTROT", "F", 'f' },
    { "GOLF", "G", 'g' }, { "HOTEL", "H", 'h' }, { "INDIA", "I", 'i' },
    { "JULIETT", "J", 'j' }, { "KILO", "K", 'k' }, { "LIMA", "L", 'l' },
    { "MIKE", "M", 'm' }, { "NOVEMBER", "N", 'n' }, { "OSCAR", "O", 'o' },
    { "PAPA", "P", 'p' }, { "QUEBEC", "Q", 'q' }, { "ROMEO", "R", 'r' },
    { "SIERRA", "S", 's' }, { "TANGO", "T", 't' }, { "UNIFORM", "U", 'u' },
    { "VICTOR", "V", 'v' }, { "WHISKEY", "W", 'w' }, { "X-RAY", "X", 'x' },
    { "YANKEE", "Y", 'y' }, { "ZULU", "Z", 'z' },
    { "ZERO", "0", '0' }, { "ONE", "1", '1' }, { "TWO", "2", '2' },
    { "THREE", "3", '3' }, { "FOUR", "4", '4' }, { "FIVE", "5", '5' },
    { "SIX", "6", '6' }, { "SEVEN", "7", '7' }, { "EIGHT", "8", '8' },
    { "NINE", "9", '9' },
    { "SPACE", "SP", ' ' }, { "ENTER", "EN", KEYS_CHAR_ENTER },
    { "DELETE", "DE", KEYS_CHAR_DELETE }, { "SHIFT", "SH", KEYS_CHAR_SHIFT },
};

/* key of a lower case letter on a QWERTY keyboard */
static uint8_t _letter(char ch)
{
#ifdef KEYS_AZERTY
    /* the letters that move on a French keyboard */
    switch (ch) {
    case 'a':
        return USAGE_Q;
    case 'q':
        return USAGE_A;
    case 'z':
        return USAGE_W;
    case 'w':
        return USAGE_Z;
    case 'm':
        return USAGE_SEMICOLON;
    default:
        break;
    }
#endif
    return (uint8_t)(USAGE_A + (ch - 'a'));
}

bool keys_lookup(char ch, uint8_t *mod, uint8_t *usage)
{
    *mod = 0;
    if (ch >= 'a' && ch <= 'z') {
        *usage = _letter(ch);
    }
    else if (ch >= 'A' && ch <= 'Z') {
        *usage = _letter((char)(ch - 'A' + 'a'));
        *mod = KEYS_MOD_SHIFT;
    }
    else if (ch >= '0' && ch <= '9') {
        *usage = ch == '0' ? USAGE_0 : (uint8_t)(USAGE_1 + (ch - '1'));
#ifdef KEYS_AZERTY
        /* the digits are shifted on a French keyboard */
        *mod = KEYS_MOD_SHIFT;
#endif
    }
    else if (ch == ' ') {
        *usage = USAGE_SPACE;
    }
    else if (ch == KEYS_CHAR_ENTER) {
        *usage = USAGE_ENTER;
    }
    else if (ch == KEYS_CHAR_DELETE) {
        *usage = USAGE_BACKSPACE;
    }
    else {
        return false;
    }
    return true;
}
