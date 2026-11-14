/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Zones of BLE tags learned with the k nearest neighbors of
 *              emlearn
 */

#include <string.h>

#include "zones.h"

/* the emlearn headers define functions, only include them here */
#define EML_NEIGHBORS_MAX_CLASSES   ZONES_MAX_ZONES
#include "eml_neighbors.h"

#define AD_FLAGS            (0x01)
#define AD_MANUFACTURER     (0xff)
#define FLAGS_GENERAL       (0x06)  /* general discoverable, no BR/EDR */
#define MAGIC               ('Z')
#define TYPE_TAG            ('T')
#define TYPE_ANCHOR         ('A')

static size_t _header(uint8_t *buf, uint8_t type, size_t payload)
{
    buf[0] = 2;
    buf[1] = AD_FLAGS;
    buf[2] = FLAGS_GENERAL;
    buf[3] = (uint8_t)(1 + 2 + 2 + payload);
    buf[4] = AD_MANUFACTURER;
    buf[5] = (uint8_t)(ZONES_COMPANY & 0xff);
    buf[6] = (uint8_t)(ZONES_COMPANY >> 8);
    buf[7] = MAGIC;
    buf[8] = type;
    return 9;
}

size_t zones_tag_adv(uint8_t *buf, uint16_t tag)
{
    size_t pos = _header(buf, TYPE_TAG, 2);

    buf[pos++] = (uint8_t)tag;
    buf[pos++] = (uint8_t)(tag >> 8);
    return pos;
}

size_t zones_anchor_adv(uint8_t *buf, const zones_report_t *r)
{
    unsigned count = r->count > ZONES_MAX_TAGS ? ZONES_MAX_TAGS : r->count;
    size_t pos = _header(buf, TYPE_ANCHOR, 4 + 3 * count);

    buf[pos++] = (uint8_t)r->anchor;
    buf[pos++] = (uint8_t)(r->anchor >> 8);
    buf[pos++] = r->seq;
    buf[pos++] = (uint8_t)count;
    for (unsigned i = 0; i < count; i++) {
        buf[pos++] = (uint8_t)r->tag[i];
        buf[pos++] = (uint8_t)(r->tag[i] >> 8);
        buf[pos++] = (uint8_t)r->rssi[i];
    }
    return pos;
}

zones_adv_type_t zones_parse(const uint8_t *ad, size_t len, uint16_t *tag,
                             zones_report_t *report)
{
    size_t pos = 0;

    while (pos + 1 < len) {
        size_t field = ad[pos];
        if (field == 0 || pos + 1 + field > len) {
            break;
        }
        const uint8_t *d = ad + pos + 2;
        size_t dlen = field - 1;
        if (ad[pos + 1] == AD_MANUFACTURER && dlen >= 4 &&
            (d[0] | (d[1] << 8)) == ZONES_COMPANY && d[2] == MAGIC) {
            if (d[3] == TYPE_TAG && dlen == 6) {
                *tag = (uint16_t)(d[4] | (d[5] << 8));
                return ZONES_ADV_TAG;
            }
            if (d[3] == TYPE_ANCHOR && dlen >= 8) {
                unsigned count = d[7];
                if (count > ZONES_MAX_TAGS || dlen != 8 + 3 * count) {
                    return ZONES_ADV_OTHER;
                }
                report->anchor = (uint16_t)(d[4] | (d[5] << 8));
                report->seq = d[6];
                report->count = (uint8_t)count;
                for (unsigned i = 0; i < count; i++) {
                    const uint8_t *t = d + 8 + 3 * i;
                    report->tag[i] = (uint16_t)(t[0] | (t[1] << 8));
                    report->rssi[i] = (int8_t)t[2];
                }
                return ZONES_ADV_ANCHOR;
            }
        }
        pos += 1 + field;
    }
    return ZONES_ADV_OTHER;
}

void zones_anchor_heard(zones_anchor_t *a, uint16_t tag, int8_t rssi)
{
    unsigned free_slot = ZONES_MAX_TAGS;

    for (unsigned i = 0; i < ZONES_MAX_TAGS; i++) {
        if (a->n[i] && a->tag[i] == tag) {
            a->sum[i] += rssi;
            a->n[i]++;
            return;
        }
        if (!a->n[i] && free_slot == ZONES_MAX_TAGS) {
            free_slot = i;
        }
    }
    if (free_slot < ZONES_MAX_TAGS) {
        a->tag[free_slot] = tag;
        a->sum[free_slot] = rssi;
        a->n[free_slot] = 1;
    }
}

void zones_anchor_report(zones_anchor_t *a, uint16_t anchor_id, zones_report_t *r)
{
    r->anchor = anchor_id;
    r->seq = a->seq++;
    r->count = 0;
    for (unsigned i = 0; i < ZONES_MAX_TAGS; i++) {
        if (a->n[i]) {
            int32_t mean = a->sum[i] / (int32_t)a->n[i];
            r->tag[r->count] = a->tag[i];
            r->rssi[r->count] = (int8_t)mean;
            r->count++;
            a->n[i] = 0;
        }
    }
}

void zones_locator_reset(zones_locator_t *loc)
{
    memset(loc, 0, sizeof(*loc));
}

static int _index(uint16_t *ids, unsigned *n, unsigned max, uint16_t id, bool add)
{
    for (unsigned i = 0; i < *n; i++) {
        if (ids[i] == id) {
            return (int)i;
        }
    }
    if (!add || *n >= max) {
        return -1;
    }
    ids[*n] = id;
    return (int)(*n)++;
}

/* a tag no anchor heard for a while, its slot can go to a new tag */
static int _stale_tag(const zones_locator_t *loc, uint32_t now)
{
    for (unsigned t = 0; t < loc->n_tags; t++) {
        bool stale = true;
        for (unsigned a = 0; a < loc->n_anchors && stale; a++) {
            stale = !loc->heard_ms[t][a] || now - loc->heard_ms[t][a] > ZONES_STALE_MS;
        }
        if (stale) {
            return (int)t;
        }
    }
    return -1;
}

void zones_locator_report(zones_locator_t *loc, const zones_report_t *r, uint32_t now)
{
    /* features keep the order the anchors were first heard in, so that the
       fingerprints learned stay valid when new anchors show up */
    int a = _index(loc->anchors, &loc->n_anchors, ZONES_MAX_ANCHORS, r->anchor, true);

    if (a < 0) {
        return;
    }
    for (unsigned i = 0; i < r->count; i++) {
        int t = _index(loc->tags, &loc->n_tags, ZONES_MAX_TAGS, r->tag[i], true);
        if (t < 0) {
            t = _stale_tag(loc, now);
            if (t < 0) {
                continue;
            }
            loc->tags[t] = r->tag[i];
            memset(loc->heard_ms[t], 0, sizeof(loc->heard_ms[t]));
        }
        loc->rssi[t][a] = r->rssi[i];
        loc->heard_ms[t][a] = now;
    }
}

int zones_fingerprint(const zones_locator_t *loc, uint16_t tag, uint32_t now,
                      int16_t features[ZONES_MAX_ANCHORS])
{
    unsigned n = loc->n_tags;
    int t = _index((uint16_t *)loc->tags, &n, ZONES_MAX_TAGS, tag, false);
    int heard = 0;

    if (t < 0) {
        return -1;
    }
    for (unsigned a = 0; a < ZONES_MAX_ANCHORS; a++) {
        bool fresh = a < loc->n_anchors && loc->heard_ms[t][a] &&
                     now - loc->heard_ms[t][a] <= ZONES_STALE_MS;
        features[a] = fresh ? loc->rssi[t][a] : ZONES_RSSI_NONE;
        heard += fresh;
    }
    return heard;
}

static int16_t _data[ZONES_MAX_ITEMS * ZONES_MAX_ANCHORS];
static int16_t _labels[ZONES_MAX_ITEMS];
static EmlNeighborsDistanceItem _distances[ZONES_MAX_ITEMS];
static unsigned _counts[ZONES_MAX_ZONES];
static EmlNeighborsModel _model = {
    .n_features = ZONES_MAX_ANCHORS,
    .n_items = 0,
    .max_items = ZONES_MAX_ITEMS,
    .data = _data,
    .labels = _labels,
    .k_neighbors = ZONES_K,
};

void zones_model_reset(void)
{
    _model.n_items = 0;
    memset(_counts, 0, sizeof(_counts));
}

int zones_model_add(const int16_t features[ZONES_MAX_ANCHORS], unsigned zone)
{
    if (zone >= ZONES_MAX_ZONES ||
        eml_neighbors_add_item(&_model, features, ZONES_MAX_ANCHORS, (int16_t)zone) != EmlOk) {
        return -1;
    }
    _counts[zone]++;
    return 0;
}

unsigned zones_model_count(unsigned zone)
{
    return zone < ZONES_MAX_ZONES ? _counts[zone] : 0;
}

int zones_model_predict(const int16_t features[ZONES_MAX_ANCHORS], unsigned *votes)
{
    int16_t zone = -1;

    if (_model.n_items < ZONES_K ||
        eml_neighbors_predict(&_model, features, ZONES_MAX_ANCHORS, _distances,
                              ZONES_MAX_ITEMS, &zone) != EmlOk) {
        return -1;
    }
    if (votes) {
        *votes = zone >= 0 ? (unsigned)eml_neighbors_votes[zone] : 0;
    }
    return zone;
}
