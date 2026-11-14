/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Zones of BLE tags from the signal strength measured by fixed
 *              anchors, learned on the device with the k nearest neighbors
 *              of emlearn. Plain C without RIOT dependencies so that it can
 *              be tested on the host.
 *
 * Three roles:
 * - a tag advertises its id
 * - an anchor hears the tags and advertises, every second, the mean signal
 *   strength of each tag over that second
 * - a locator hears the anchors and builds for each tag a fingerprint, the
 *   signal strength measured by each anchor. Fingerprints taught with a
 *   zone feed a k nearest neighbors model, which then gives the zone of the
 *   tags.
 *
 * The advertisements carry manufacturer specific data with the test company
 * id 0xFFFF, then 'Z' and a type:
 * - tag:    'T', tag id (16 bits)
 * - anchor: 'A', anchor id (16 bits), sequence number, count, then for each
 *           tag its id (16 bits) and its mean RSSI (8 bits, signed)
 * All the 16-bit values are little endian.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZONES_COMPANY           (0xffffU)   /**< test company id */
#define ZONES_MAX_TAGS          (6)         /**< tags heard by an anchor,
                                                 tags followed by a locator */
#define ZONES_MAX_ANCHORS       (24)        /**< anchors used as features */
#define ZONES_MAX_ITEMS         (480)       /**< fingerprints learned */
#define ZONES_MAX_ZONES         (10)        /**< zones 0 to 9 */
#define ZONES_K                 (5)         /**< neighbors that vote */
#define ZONES_RSSI_NONE         (-100)      /**< tag not heard by an anchor */
#define ZONES_STALE_MS          (3000U)     /**< age of a report still used */
#define ZONES_ADV_MAX           (31)

/**
 * @brief   Type of an advertisement
 */
typedef enum {
    ZONES_ADV_OTHER,                /**< not one of ours */
    ZONES_ADV_TAG,
    ZONES_ADV_ANCHOR,
} zones_adv_type_t;

/**
 * @brief   Report of an anchor
 */
typedef struct {
    uint16_t anchor;
    uint8_t seq;
    uint8_t count;
    uint16_t tag[ZONES_MAX_TAGS];
    int8_t rssi[ZONES_MAX_TAGS];
} zones_report_t;

/**
 * @brief   Advertising data of a tag (flags and manufacturer data)
 *
 * @return  length, at most ZONES_ADV_MAX
 */
size_t zones_tag_adv(uint8_t *buf, uint16_t tag);

/**
 * @brief   Advertising data of an anchor report
 *
 * @return  length, at most ZONES_ADV_MAX
 */
size_t zones_anchor_adv(uint8_t *buf, const zones_report_t *report);

/**
 * @brief   Parse advertising data
 *
 * @param[out] tag      tag id, for a tag advertisement
 * @param[out] report   report, for an anchor advertisement
 */
zones_adv_type_t zones_parse(const uint8_t *ad, size_t len, uint16_t *tag,
                             zones_report_t *report);

/**
 * @brief   Signal strength of the tags heard by an anchor over a period
 */
typedef struct {
    uint16_t tag[ZONES_MAX_TAGS];
    int32_t sum[ZONES_MAX_TAGS];
    uint16_t n[ZONES_MAX_TAGS];
    uint8_t seq;
} zones_anchor_t;

/**
 * @brief   Add a measurement of a tag
 */
void zones_anchor_heard(zones_anchor_t *a, uint16_t tag, int8_t rssi);

/**
 * @brief   Report of the period that ends, the mean signal strength of each
 *          tag heard, and start a new period
 */
void zones_anchor_report(zones_anchor_t *a, uint16_t anchor_id, zones_report_t *report);

/**
 * @brief   Locator: fingerprints of the tags and the model
 */
typedef struct {
    uint16_t anchors[ZONES_MAX_ANCHORS];    /**< id of each feature */
    unsigned n_anchors;
    uint16_t tags[ZONES_MAX_TAGS];
    unsigned n_tags;
    int8_t rssi[ZONES_MAX_TAGS][ZONES_MAX_ANCHORS];
    uint32_t heard_ms[ZONES_MAX_TAGS][ZONES_MAX_ANCHORS];
} zones_locator_t;

/**
 * @brief   Forget the anchors and tags
 */
void zones_locator_reset(zones_locator_t *loc);

/**
 * @brief   Use a report of an anchor
 */
void zones_locator_report(zones_locator_t *loc, const zones_report_t *report,
                          uint32_t now_ms);

/**
 * @brief   Fingerprint of a tag: the RSSI measured by each anchor, recent
 *          reports only
 *
 * @return  number of anchors that hear the tag, -1 for an unknown tag
 */
int zones_fingerprint(const zones_locator_t *loc, uint16_t tag, uint32_t now_ms,
                      int16_t features[ZONES_MAX_ANCHORS]);

/**
 * @brief   Forget the fingerprints learned
 */
void zones_model_reset(void);

/**
 * @brief   Learn a fingerprint of a zone
 *
 * @return  0, -1 when the model is full or the zone invalid
 */
int zones_model_add(const int16_t features[ZONES_MAX_ANCHORS], unsigned zone);

/**
 * @brief   Number of fingerprints learned for a zone
 */
unsigned zones_model_count(unsigned zone);

/**
 * @brief   Zone of a fingerprint
 *
 * @param[out] votes    neighbors among the k that voted for it
 *
 * @return  the zone, -1 when too few fingerprints were learned
 */
int zones_model_predict(const int16_t features[ZONES_MAX_ANCHORS], unsigned *votes);

#ifdef __cplusplus
}
#endif
