/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the zones: advertisements encoded and parsed, then a
 * simulated floor of 40 x 10 m with 16 anchors and 3 zones along it. Tags
 * at 30 places teach the zones, tags at 30 other places are located. The
 * signal strength follows a log-distance path loss with shadowing and
 * noise, every message goes through the advertisement encoding.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zones.h"

#define ANCHORS     (16)
#define WIDTH       (40.0)
#define DEPTH       (10.0)
#define ZONES       (3)
#define PLACES      (60)
#define TEACH_S     (15)
#define TEST_S      (10)

static int _failures;

static double _uniform(void)
{
    return (rand() + 0.5) / ((double)RAND_MAX + 1.0);
}

static double _normal(void)
{
    return sqrt(-2 * log(_uniform())) * cos(2 * M_PI * _uniform());
}

static void _check(const char *what, int ok)
{
    printf("%s: %s\n", what, ok ? "ok" : "FAIL");
    _failures += !ok;
}

static void _test_adv(void)
{
    uint8_t buf[ZONES_ADV_MAX];
    zones_report_t r = { .anchor = 0xbeef, .seq = 42, .count = ZONES_MAX_TAGS };
    zones_report_t out;
    uint16_t tag = 0;

    for (unsigned i = 0; i < ZONES_MAX_TAGS; i++) {
        r.tag[i] = (uint16_t)(0x1000 + i);
        r.rssi[i] = (int8_t)(-40 - 7 * (int)i);
    }
    size_t len = zones_anchor_adv(buf, &r);
    _check("anchor advertisement fits", len <= ZONES_ADV_MAX);
    _check("anchor advertisement parsed",
           zones_parse(buf, len, &tag, &out) == ZONES_ADV_ANCHOR && out.anchor == 0xbeef &&
           out.seq == 42 && out.count == ZONES_MAX_TAGS &&
           memcmp(out.tag, r.tag, sizeof(r.tag)) == 0 &&
           memcmp(out.rssi, r.rssi, sizeof(r.rssi)) == 0);
    len = zones_tag_adv(buf, 0x1234);
    _check("tag advertisement parsed",
           zones_parse(buf, len, &tag, &out) == ZONES_ADV_TAG && tag == 0x1234);
    const uint8_t other[] = { 2, 1, 6, 5, 0xff, 0x59, 0x00, 'Z', 'T' };
    _check("other advertisement ignored",
           zones_parse(other, sizeof(other), &tag, &out) == ZONES_ADV_OTHER);
    const uint8_t broken[] = { 2, 1, 6, 30, 0xff, 0xff };
    _check("truncated advertisement ignored",
           zones_parse(broken, sizeof(broken), &tag, &out) == ZONES_ADV_OTHER);
}

typedef struct {
    double x, y;
} point_t;

static point_t _anchors[ANCHORS];
static double _shadow[PLACES][ANCHORS];

static unsigned _zone(point_t p)
{
    unsigned z = (unsigned)(p.x / (WIDTH / ZONES));
    return z >= ZONES ? ZONES - 1 : z;
}

/* one second of a tag at a place: anchors hear it about 10 times, report,
   the locator hears the reports. Returns the fingerprint. */
static void _second(zones_anchor_t *anchors, zones_locator_t *loc, unsigned place, point_t p,
                    uint16_t tag, uint32_t now, int16_t features[ZONES_MAX_ANCHORS])
{
    uint8_t buf[ZONES_ADV_MAX];

    for (unsigned a = 0; a < ANCHORS; a++) {
        double d = hypot(p.x - _anchors[a].x, p.y - _anchors[a].y);
        d = d < 0.5 ? 0.5 : d;
        for (unsigned s = 0; s < 10; s++) {
            double rssi = -59 - 25 * log10(d) + _shadow[place][a] + 4 * _normal();
            if (rssi > -95) {
                zones_anchor_heard(&anchors[a], tag, (int8_t)lround(rssi));
            }
        }
        zones_report_t r, parsed;
        uint16_t t;
        zones_anchor_report(&anchors[a], (uint16_t)(0xa000 + a), &r);
        size_t len = zones_anchor_adv(buf, &r);
        if (zones_parse(buf, len, &t, &parsed) == ZONES_ADV_ANCHOR) {
            zones_locator_report(loc, &parsed, now);
        }
    }
    zones_fingerprint(loc, tag, now, features);
}

static void _test_floor(void)
{
    static zones_anchor_t anchors[ANCHORS];
    static zones_locator_t loc;
    point_t places[PLACES];
    int16_t f[ZONES_MAX_ANCHORS];
    uint32_t now = 1000;
    unsigned right = 0, total = 0, unheard = 0;
    unsigned confusion[ZONES][ZONES] = { 0 };

    srand(1234);
    for (unsigned a = 0; a < ANCHORS; a++) {
        _anchors[a] = (point_t){ (a % 8 + 0.5) * WIDTH / 8, a < 8 ? 1.0 : DEPTH - 1.0 };
    }
    for (unsigned p = 0; p < PLACES; p++) {
        places[p] = (point_t){ _uniform() * WIDTH, _uniform() * DEPTH };
        for (unsigned a = 0; a < ANCHORS; a++) {
            _shadow[p][a] = 3 * _normal();
        }
    }
    memset(anchors, 0, sizeof(anchors));
    zones_locator_reset(&loc);
    zones_model_reset();

    /* even places teach, odd places are located */
    for (unsigned p = 0; p < PLACES; p += 2) {
        for (unsigned s = 0; s < TEACH_S; s++, now += 1000) {
            _second(anchors, &loc, p, places[p], 0x7a61, now, f);
            if (zones_model_add(f, _zone(places[p])) != 0) {
                unheard++;
            }
        }
    }
    printf("learned %u %u %u fingerprints\n", zones_model_count(0), zones_model_count(1),
           zones_model_count(2));
    for (unsigned p = 1; p < PLACES; p += 2) {
        for (unsigned s = 0; s < TEST_S; s++, now += 1000) {
            unsigned votes;
            _second(anchors, &loc, p, places[p], 0x7a62, now, f);
            int z = zones_model_predict(f, &votes);
            total++;
            if (z >= 0) {
                confusion[_zone(places[p])][z]++;
                right += (unsigned)z == _zone(places[p]);
            }
        }
    }
    printf("located %u of %u seconds in the right zone (%.1f%%)\n", right, total,
           100.0 * right / total);
    for (unsigned z = 0; z < ZONES; z++) {
        printf("  zone %u: %3u %3u %3u\n", z, confusion[z][0], confusion[z][1], confusion[z][2]);
    }
    _check("all the fingerprints learned", unheard == 0);
    _check("zones found at unseen places", right >= total * 8 / 10);

    /* a tag gone silent fades out of the fingerprint */
    int heard = zones_fingerprint(&loc, 0x7a62, now + ZONES_STALE_MS + 1000, f);
    _check("stale reports dropped", heard == 0 && f[0] == ZONES_RSSI_NONE);
    _check("unknown tag", zones_fingerprint(&loc, 0x0001, now, f) == -1);

    /* more tags than slots, one after the other: the silent ones make room */
    unsigned followed = 0;
    for (unsigned i = 0; i < 3 * ZONES_MAX_TAGS; i++) {
        now += ZONES_STALE_MS + 1000;
        uint16_t tag = (uint16_t)(0x5000 + i);
        for (unsigned s = 0; s < 3; s++, now += 1000) {
            _second(anchors, &loc, 0, places[0], tag, now, f);
        }
        followed += zones_fingerprint(&loc, tag, now - 1000, f) > 0;
    }
    _check("tags one after the other all followed", followed == 3 * ZONES_MAX_TAGS);
}

int main(void)
{
    _test_adv();
    _test_floor();
    puts(_failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
