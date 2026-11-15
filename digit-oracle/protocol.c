/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Line protocol between tools/send_digits.py and the board
 */

#include <stdio.h>
#include <string.h>

#include "protocol.h"

static int _hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int proto_parse(const char *line, proto_request_t *req)
{
    req->cmd = line[0];
    req->label = -1;

    if (req->cmd == '?') {
        return 0;
    }
    if ((req->cmd != 'D' && req->cmd != 'N') || line[1] != ' ' ||
        line[3] != ' ') {
        return -1;
    }
    if (line[2] >= '0' && line[2] <= '9') {
        req->label = line[2] - '0';
    }
    else if (line[2] != '-') {
        return -1;
    }

    const char *hex = &line[4];
    for (unsigned i = 0; i < DIGITS_PIXELS; i++) {
        int hi = _hex(hex[2 * i]);
        int lo = hi < 0 ? -1 : _hex(hex[2 * i + 1]);
        if (lo < 0) {
            return -1;
        }
        req->image[i] = (hi << 4) | lo;
    }
    return hex[2 * DIGITS_PIXELS] == '\0' ? 0 : -1;
}

size_t proto_format_result(char *buf, size_t len, int digit, unsigned us,
                           const float proba[DIGITS_CLASSES])
{
    int n = snprintf(buf, len, "P %d %u", digit, us);

    for (unsigned i = 0; i < DIGITS_CLASSES && n > 0 && (size_t)n < len; i++) {
        n += snprintf(buf + n, len - n, " %d", (int)(proba[i] * 100.0f + 0.5f));
    }
    if (n > 0 && (size_t)n + 1 < len) {
        buf[n++] = '\n';
        buf[n] = '\0';
    }
    if (n < 0) {
        return 0;
    }
    /* the length written, even when it was cut */
    return (size_t)n < len ? (size_t)n : len - 1;
}

size_t proto_format_image(char *buf, const uint8_t *image)
{
    static const char digits[] = "0123456789abcdef";
    size_t n = 0;

    buf[n++] = 'B';
    buf[n++] = ' ';
    for (unsigned i = 0; i < DIGITS_PIXELS; i++) {
        buf[n++] = digits[image[i] >> 4];
        buf[n++] = digits[image[i] & 0xf];
    }
    buf[n++] = '\n';
    buf[n] = '\0';
    return n;
}
