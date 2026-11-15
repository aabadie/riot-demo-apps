/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the packets of the dashboard: prints "<name> <hex>" lines,
 * decoded by decode.mjs with the protocol.js of the page, and checks the
 * cleaning of the texts written by the phone.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"

static int _failures;

static void _hex(const char *name, const uint8_t *buf, size_t len)
{
    printf("%s ", name);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }
    printf("\n");
}

static void _text(const char *in, const char *expected)
{
    char out[DASH_TEXT_MAX + 1];

    dash_clean_text(out, (const uint8_t *)in, strlen(in));
    bool ok = strcmp(out, expected) == 0;
    printf("# text -> \"%s\" %s\n", out, ok ? "ok" : "FAIL");
    _failures += !ok;
}

int main(void)
{
    uint8_t s[DASH_SENSORS_LEN], st[DASH_STATUS_LEN];

    dash_sensors_t full = {
        .has_accel = true, .accel_g = { 0.012f, -0.981f, 1.5f },
        .temp_c = 22.57f, .hum = 41.25f, .press_hpa = 1012.3f, .lux = 87,
        .has_rgb = true, .rgb = { 120, 340, 65535 },
    };
    dash_pack_sensors(s, &full);
    _hex("sensors_full", s, sizeof(s));

    dash_sensors_t none = { .temp_c = NAN, .hum = NAN, .press_hpa = NAN, .lux = NAN };
    dash_pack_sensors(s, &none);
    _hex("sensors_none", s, sizeof(s));

    dash_sensors_t cold = { .temp_c = -12.34f, .hum = NAN, .press_hpa = NAN, .lux = NAN };
    dash_pack_sensors(s, &cold);
    _hex("sensors_cold", s, sizeof(s));

    dash_status_t a = {
        .match = 2, .distance = 1.26f, .rssi = -63, .proximity = 300,
        .counts = { 10, 0, 20, 300, 0, 1 }, .buttons = 2 | DASH_STATUS_TEACHING,
    };
    dash_pack_status(st, &a);
    _hex("status_match", st, sizeof(st));

    dash_status_t far = { .match = DASH_MATCH_FAR };
    dash_pack_status(st, &far);
    _hex("status_far", st, sizeof(st));

    _text("Hello, RIOT!", "HELLO, RIOT!");
    _text("caf\xc3\xa9 [ok]", "CAF? ?OK?");
    _text("line\nbreak", "LINE BREAK");
    char longer[100];
    memset(longer, 'a', 99);
    longer[99] = '\0';
    char expected[DASH_TEXT_MAX + 1];
    memset(expected, 'A', DASH_TEXT_MAX);
    expected[DASH_TEXT_MAX] = '\0';
    _text(longer, expected);

    printf("# %s\n", _failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
