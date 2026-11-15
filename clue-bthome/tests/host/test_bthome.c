/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the BTHome encoder and of the button events. Prints the
 * service data of a few advertisements, "<name> <hex>", for decode.py, and
 * checks the button events itself.
 */

#include <stdio.h>
#include <string.h>

#include "bthome.h"

static int _failures;

static void _print(const char *name, const bthome_data_t *d)
{
    uint8_t buf[26];
    size_t len = bthome_encode(buf, sizeof(buf), d);

    if (len == 0 || len > 26) {
        printf("FAIL %s: length %zu\n", name, len);
        _failures++;
        return;
    }
    printf("%s ", name);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }
    printf("\n");
}

/* feed a button with a list of (held, duration ms) steps, return the events */
static void _sequence(const char *name, const unsigned *steps, unsigned n,
                      const uint8_t *expected, unsigned n_expected)
{
    bthome_button_t b = { 0 };
    uint8_t events[8];
    unsigned count = 0;
    uint32_t now = 1000;

    for (unsigned s = 0; s < n; s++) {
        bool held = s % 2 == 0;
        for (unsigned t = 0; t < steps[s]; t += 20, now += 20) {
            uint8_t ev = bthome_button_update(&b, held, now);
            if (ev && count < 8) {
                events[count++] = ev;
            }
        }
    }
    bool ok = count == n_expected && memcmp(events, expected, count) == 0;
    printf("# buttons %s: %u events %s\n", name, count, ok ? "ok" : "FAIL");
    if (!ok) {
        _failures++;
    }
}

/* 12 s of a running board, a frame every 20 ms: the sensors change every
   2 s, the motion at 3.1 s, A is pressed at 1.9 s and held from 6 s, B is
   pressed twice at 2.3 s. Prints each new packet id as "stream <hex>" for
   decode.py, which must see each event once. */
static void _stream(void)
{
    bthome_data_t d = {
        .has_temp = true, .temp_c = 20.0f,
        .has_motion = true,
    };
    bthome_button_t btn[BTHOME_BUTTONS] = { 0 };
    bthome_sched_t sched = { 0 };

    for (uint32_t now = 0; now <= 12000; now += 20) {
        bool held[BTHOME_BUTTONS] = {
            (now >= 1900 && now < 2000) || (now >= 6000 && now < 7500),
            (now >= 2300 && now < 2400) || (now >= 2500 && now < 2600),
        };
        uint8_t events[BTHOME_BUTTONS];
        bool changed = false;

        for (unsigned i = 0; i < BTHOME_BUTTONS; i++) {
            events[i] = bthome_button_update(&btn[i], held[i], now);
        }
        if (now > 0 && now % 2000 == 0) {
            d.temp_c += 0.25f;
            changed = true;
        }
        if (now == 3100) {
            d.moving = true;
            changed = true;
        }
        if (bthome_sched_update(&sched, &d, events, changed, now)) {
            d.packet_id++;
            _print("stream", &d);
        }
    }
}

int main(void)
{
    bthome_data_t d = {
        .packet_id = 7,
        .has_temp = true, .temp_c = 21.37f,
        .has_press = true, .press_hpa = 1013.25f,
        .has_lux = true, .lux = 312.0f,
        .has_motion = true, .moving = true, .vibration = false,
        .has_hum = true, .hum = 45.4f,
        .button = { BTHOME_EV_NONE, BTHOME_EV_NONE },
    };
    _print("full", &d);

    d.packet_id = 8;
    d.temp_c = -5.5f;
    d.moving = false;
    d.vibration = true;
    d.button[0] = BTHOME_EV_DOUBLE;
    d.button[1] = BTHOME_EV_LONG;
    _print("events", &d);

    bthome_data_t few = { .packet_id = 9, .has_temp = true, .temp_c = 30.0f };
    _print("few", &few);

    /* sequences of held, released, held... durations */
    const unsigned press[] = { 100, 600 };
    const uint8_t e_press[] = { BTHOME_EV_PRESS };
    _sequence("press", press, 2, e_press, 1);
    const unsigned dbl[] = { 100, 150, 100, 600 };
    const uint8_t e_dbl[] = { BTHOME_EV_DOUBLE };
    _sequence("double", dbl, 4, e_dbl, 1);
    const unsigned tri[] = { 80, 120, 80, 120, 80, 600 };
    const uint8_t e_tri[] = { BTHOME_EV_TRIPLE };
    _sequence("triple", tri, 6, e_tri, 1);
    const unsigned lng[] = { 1500, 600 };
    const uint8_t e_lng[] = { BTHOME_EV_LONG };
    _sequence("long", lng, 2, e_lng, 1);
    const unsigned two[] = { 100, 600, 100, 600 };
    const uint8_t e_two[] = { BTHOME_EV_PRESS, BTHOME_EV_PRESS };
    _sequence("two presses", two, 4, e_two, 2);

    _stream();

    printf("# %s\n", _failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
