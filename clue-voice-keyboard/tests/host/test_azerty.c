/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Key codes of the French layout
 */

#include <stdio.h>

#include "keys.h"

typedef struct {
    char ch;
    uint8_t mod;
    uint8_t usage;
} expect_t;

int main(void)
{
    static const expect_t expected[] = {
        { 'a', 0, 0x14 }, { 'q', 0, 0x04 }, { 'z', 0, 0x1a }, { 'w', 0, 0x1d },
        { 'm', 0, 0x33 }, { 'b', 0, 0x05 }, { 'A', KEYS_MOD_SHIFT, 0x14 },
        { '1', KEYS_MOD_SHIFT, 0x1e }, { '0', KEYS_MOD_SHIFT, 0x27 },
        { ' ', 0, 0x2c },
    };

    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        uint8_t mod, usage;
        if (!keys_lookup(expected[i].ch, &mod, &usage) ||
            mod != expected[i].mod || usage != expected[i].usage) {
            printf("FAIL: AZERTY key of '%c'\n", expected[i].ch);
            return 1;
        }
    }
    puts("PASS: AZERTY key codes");
    return 0;
}
