/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the packets: prints "<name> <hex>" lines for decode.mjs,
 * which decodes them with the protocol.js of the page, and checks the
 * control commands itself.
 */

#include <math.h>
#include <stdio.h>

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

static void _control(const char *name, const uint8_t *buf, size_t len, int expected)
{
    uint8_t cmd, arg;
    int res = motion_parse_control(buf, len, &cmd, &arg);
    bool ok = (res == 0) == (expected == 0);

    printf("# control %s: %s\n", name, ok ? "ok" : "FAIL");
    _failures += !ok;
}

int main(void)
{
    uint8_t buf[16];

    /* 90 degrees about x: w = x = 0.7071 */
    motion_t m = {
        .seq = 200, .flags = MOTION_FLAG_A | MOTION_FLAG_MAG,
        .q = { 0.70710678f, 0.70710678f, 0, 0 },
    };
    motion_pack(buf, &m);
    _hex("motion_roll", buf, MOTION_LEN);

    motion_t neg = { .seq = 1, .flags = MOTION_FLAG_B | MOTION_FLAG_CALIB,
                     .q = { -1, 0, -0.5f, 0.25f } };
    motion_pack(buf, &neg);
    _hex("motion_neg", buf, MOTION_LEN);

    const float g[3] = { 0.012f, -0.981f, 1.5f };
    const float dps[3] = { 123.4f, -999.9f, 0.1f };
    motion_pack_raw(buf, g, dps);
    _hex("raw", buf, MOTION_RAW_LEN);

    motion_pack_info(buf, 104, 50, true, 6);
    _hex("info", buf, MOTION_INFO_LEN);

    const uint8_t recenter[] = { MOTION_CMD_RECENTER };
    const uint8_t mode9[] = { MOTION_CMD_MODE, 9 };
    const uint8_t mode7[] = { MOTION_CMD_MODE, 7 };
    const uint8_t rate50[] = { MOTION_CMD_RATE, 50 };
    const uint8_t rate60[] = { MOTION_CMD_RATE, 60 };
    const uint8_t unknown[] = { 9, 0 };
    _control("recenter", recenter, sizeof(recenter), 0);
    _control("mode 9", mode9, sizeof(mode9), 0);
    _control("mode 7 refused", mode7, sizeof(mode7), -1);
    _control("rate 50", rate50, sizeof(rate50), 0);
    _control("rate 60 refused", rate60, sizeof(rate60), -1);
    _control("unknown refused", unknown, sizeof(unknown), -1);
    _control("empty refused", recenter, 0, -1);

    printf("# %s\n", _failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
