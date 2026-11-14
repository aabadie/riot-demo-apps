/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host build of the board side of the protocol, reading the
 *              requests on stdin, to test digits.c and protocol.c with
 *              tools/send_digits.py --exec
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "digits.h"
#include "protocol.h"

static char _line[PROTO_LINE_MAX + 2];
static proto_request_t _req;

static unsigned _now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000000u + ts.tv_nsec / 1000u);
}

int main(void)
{
    char out[96];

    while (fgets(_line, sizeof(_line), stdin)) {
        _line[strcspn(_line, "\r\n")] = '\0';
        if (proto_parse(_line, &_req) != 0) {
            puts("E malformed line");
            fflush(stdout);
            continue;
        }
        if (_req.cmd == '?') {
            puts("I digit-oracle " PROTO_VERSION);
            fflush(stdout);
            continue;
        }

        static uint8_t norm[DIGITS_PIXELS];
        const uint8_t *image = _req.image;
        if (_req.cmd == 'N') {
            if (digits_normalize(_req.image, norm) != 0) {
                puts("E empty image");
                fflush(stdout);
                continue;
            }
            image = norm;
        }

        float proba[DIGITS_CLASSES];
        unsigned start = _now_us();
        int digit = digits_classify(image, proba);
        unsigned us = _now_us() - start;
        proto_format_result(out, sizeof(out), digit, us, proba);
        fputs(out, stdout);
        fflush(stdout);
    }
    return 0;
}
