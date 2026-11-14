/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       BLE Zones: in which zone is a BLE tag, learned on the device
 *
 * Every node runs the same firmware, in one of three roles, set with the
 * `role` command:
 * - anchor (the default): hears the tags and advertises, every second, the
 *   mean signal strength of each tag
 * - tag: advertises its id 10 times per second
 * - off: only listens
 *
 * Every node is also a locator: it hears the reports of the anchors and
 * builds the fingerprint of each tag, the signal strength measured by each
 * anchor. `teach <tag> <zone>` learns the fingerprints of a tag in a zone
 * for a few seconds, then the node gives the zone of the tags with the k
 * nearest neighbors of emlearn, every second:
 *
 *     Z <tag> <zone> <votes> <anchors that hear it>
 *
 * See zones.h for the advertisements.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "mutex.h"
#include "nimble_riot.h"
#include "nimble_scanner.h"
#include "shell.h"
#include "thread.h"
#include "ztimer.h"

#include "zones.h"

#define TICK_MS         (1000U)
#define TAG_ADV_MS      (100U)
#define ANCHOR_ADV_MS   (200U)
#define TEACH_S         (15U)

typedef enum {
    ROLE_OFF,
    ROLE_ANCHOR,
    ROLE_TAG,
} role_t;

static const char *_role_names[] = { "off", "anchor", "tag" };

static mutex_t _lock = MUTEX_INIT;
static uint16_t _id;
static role_t _role = ROLE_ANCHOR;
static zones_anchor_t _anchor;
static zones_locator_t _locator;
static bool _verbose;
static uint16_t _teach_tag;
static unsigned _teach_zone, _teach_left;
static char _tick_stack[THREAD_STACKSIZE_DEFAULT + 1024];

static void _scan_cb(uint8_t type, const ble_addr_t *addr, const nimble_scanner_info_t *info,
                     const uint8_t *ad, size_t len)
{
    (void)type;
    (void)addr;
    zones_report_t report;
    uint16_t tag;

    zones_adv_type_t t = zones_parse(ad, len, &tag, &report);
    if (t == ZONES_ADV_OTHER) {
        return;
    }
    mutex_lock(&_lock);
    if (t == ZONES_ADV_TAG && _role == ROLE_ANCHOR) {
        zones_anchor_heard(&_anchor, tag, info->rssi);
    }
    else if (t == ZONES_ADV_ANCHOR) {
        zones_locator_report(&_locator, &report, ztimer_now(ZTIMER_MSEC));
    }
    mutex_unlock(&_lock);
}

static void _advertise(const uint8_t *data, size_t len, unsigned itvl_ms)
{
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_NON,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(itvl_ms),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(itvl_ms),
    };

    ble_gap_adv_set_data(data, len);
    if (!ble_gap_adv_active()) {
        int res = ble_gap_adv_start(nimble_riot_own_addr_type, NULL, BLE_HS_FOREVER,
                                    &params, NULL, NULL);
        if (res != 0) {
            printf("cannot advertise: %d\n", res);
        }
    }
}

static void _set_role(role_t role)
{
    uint8_t buf[ZONES_ADV_MAX];

    ble_gap_adv_stop();
    mutex_lock(&_lock);
    _role = role;
    memset(&_anchor, 0, sizeof(_anchor));
    mutex_unlock(&_lock);
    if (role == ROLE_TAG) {
        _advertise(buf, zones_tag_adv(buf, _id), TAG_ADV_MS);
    }
    else if (role == ROLE_ANCHOR) {
        zones_report_t empty = { .anchor = _id };
        _advertise(buf, zones_anchor_adv(buf, &empty), ANCHOR_ADV_MS);
    }
    printf("role %s\n", _role_names[role]);
}

static void _locate(uint32_t now)
{
    int16_t f[ZONES_MAX_ANCHORS];

    for (unsigned t = 0; t < _locator.n_tags; t++) {
        uint16_t tag = _locator.tags[t];
        int heard = zones_fingerprint(&_locator, tag, now, f);
        if (heard <= 0) {
            continue;
        }
        if (_verbose) {
            printf("F %04x", tag);
            for (unsigned a = 0; a < _locator.n_anchors; a++) {
                printf(" %d", f[a]);
            }
            puts("");
        }
        if (_teach_left && tag == _teach_tag) {
            if (zones_model_add(f, _teach_zone) != 0) {
                puts("model full");
                _teach_left = 0;
                continue;
            }
            _teach_left--;
            printf("T %04x zone %u, %u fingerprints%s\n", tag, _teach_zone,
                   zones_model_count(_teach_zone), _teach_left ? "" : ", done");
            continue;
        }
        unsigned votes = 0;
        int zone = zones_model_predict(f, &votes);
        if (zone >= 0) {
            printf("Z %04x %d %u %d\n", tag, zone, votes, heard);
        }
    }
}

static void *_tick(void *arg)
{
    (void)arg;
    uint32_t last = ztimer_now(ZTIMER_MSEC);
    uint8_t buf[ZONES_ADV_MAX];

    while (1) {
        ztimer_periodic_wakeup(ZTIMER_MSEC, &last, TICK_MS);
        mutex_lock(&_lock);
        if (_role == ROLE_ANCHOR) {
            zones_report_t report;
            zones_anchor_report(&_anchor, _id, &report);
            size_t len = zones_anchor_adv(buf, &report);
            ble_gap_adv_set_data(buf, len);
            if (_verbose && report.count) {
                printf("A");
                for (unsigned i = 0; i < report.count; i++) {
                    printf(" %04x:%d", report.tag[i], report.rssi[i]);
                }
                puts("");
            }
        }
        _locate(ztimer_now(ZTIMER_MSEC));
        mutex_unlock(&_lock);
    }
    return NULL;
}

static int _cmd_role(int argc, char **argv)
{
    if (argc == 2) {
        for (unsigned r = 0; r < ARRAY_SIZE(_role_names); r++) {
            if (strcmp(argv[1], _role_names[r]) == 0) {
                _set_role((role_t)r);
                return 0;
            }
        }
    }
    printf("usage: %s off|anchor|tag (now %s)\n", argv[0], _role_names[_role]);
    return 1;
}

static int _cmd_teach(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        printf("usage: %s <tag id in hex> <zone 0-%u> [seconds]\n", argv[0],
               ZONES_MAX_ZONES - 1);
        return 1;
    }
    unsigned zone = (unsigned)atoi(argv[2]);
    if (zone >= ZONES_MAX_ZONES) {
        puts("invalid zone");
        return 1;
    }
    mutex_lock(&_lock);
    _teach_tag = (uint16_t)strtoul(argv[1], NULL, 16);
    _teach_zone = zone;
    _teach_left = argc == 4 ? (unsigned)atoi(argv[3]) : TEACH_S;
    mutex_unlock(&_lock);
    printf("teaching zone %u with tag %04x\n", zone, _teach_tag);
    return 0;
}

static int _cmd_forget(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mutex_lock(&_lock);
    zones_model_reset();
    zones_locator_reset(&_locator);
    _teach_left = 0;
    mutex_unlock(&_lock);
    puts("forgot the zones, the anchors and the tags");
    return 0;
}

static int _cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mutex_lock(&_lock);
    printf("id %04x role %s, %u anchors:", _id, _role_names[_role], _locator.n_anchors);
    for (unsigned a = 0; a < _locator.n_anchors; a++) {
        printf(" %04x", _locator.anchors[a]);
    }
    printf("\n%u tags:", _locator.n_tags);
    for (unsigned t = 0; t < _locator.n_tags; t++) {
        printf(" %04x", _locator.tags[t]);
    }
    printf("\nfingerprints per zone:");
    for (unsigned z = 0; z < ZONES_MAX_ZONES; z++) {
        printf(" %u", zones_model_count(z));
    }
    puts("");
    mutex_unlock(&_lock);
    return 0;
}

static int _cmd_verbose(int argc, char **argv)
{
    _verbose = argc == 2 && strcmp(argv[1], "on") == 0;
    printf("verbose %s\n", _verbose ? "on" : "off");
    return 0;
}

static const shell_command_t _commands[] = {
    { "role", "off, anchor or tag", _cmd_role },
    { "teach", "learn the zone of a tag", _cmd_teach },
    { "forget", "forget the zones, anchors and tags", _cmd_forget },
    { "status", "anchors, tags and zones known", _cmd_status },
    { "verbose", "print the reports and fingerprints (on|off)", _cmd_verbose },
    { NULL, NULL, NULL },
};

int main(void)
{
    uint8_t addr[6] = { 0 };

    ble_hs_id_copy_addr(nimble_riot_own_addr_type, addr, NULL);
    /* NimBLE stores the address in reverse order */
    _id = (uint16_t)((addr[1] << 8) | addr[0]);
    printf("BLE Zones, id %04x\n", _id);

    zones_locator_reset(&_locator);
    zones_model_reset();

    nimble_scanner_cfg_t cfg = {
        .itvl_ms = 100,
        .win_ms = 100,
        .flags = NIMBLE_SCANNER_PASSIVE | NIMBLE_SCANNER_PHY_1M,
    };
    nimble_scanner_init(&cfg, _scan_cb);
    nimble_scanner_set_scan_duration(BLE_HS_FOREVER);
    nimble_scanner_start();
    _set_role(ROLE_ANCHOR);

    thread_create(_tick_stack, sizeof(_tick_stack), THREAD_PRIORITY_MAIN - 1, 0, _tick,
                  NULL, "zones");

    char line[SHELL_DEFAULT_BUFSIZE];
    shell_run(_commands, line, sizeof(line));
    return 0;
}
