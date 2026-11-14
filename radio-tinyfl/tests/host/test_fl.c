/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Host test of the federated naive Bayes
 *
 * - merging the statistics of two halves of a dataset gives the statistics
 *   of the whole dataset
 * - the messages keep the statistics
 * - the table keeps the newest statistics of each node
 * - 8 nodes that see 2 classes each: the local models are right about half
 *   of the time, the federated model as often as a model learned from all
 *   the samples in one place
 * - relaying over a line of nodes reaches FL_MAX_HOPS hops
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fl.h"

#define NODES       (8U)
#define SAMPLES     (100U)
#define LINE        (7U)

static int _failed;

static void _check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        _failed = 1;
    }
}

static float _var(const fl_class_stats_t *s, unsigned f)
{
    return s->n > 1 ? s->m2[f] / (s->n - 1) : 0;
}

static void _test_merge(void)
{
    fl_stats_t all, a, b;
    fl_rng_t rng;
    float x[FL_FEATURES];

    fl_stats_reset(&all);
    fl_stats_reset(&a);
    fl_stats_reset(&b);
    fl_rng_seed(&rng, 1);
    for (unsigned i = 0; i < 300; i++) {
        unsigned c = i % FL_CLASSES;
        fl_draw(&rng, c, x);
        fl_stats_add(&all, c, x);
        fl_stats_add(i < 70 ? &a : &b, c, x);
    }
    fl_stats_merge(&a, &b);
    float err = 0;
    for (unsigned c = 0; c < FL_CLASSES; c++) {
        _check(a.c[c].n == all.c[c].n, "merged count");
        for (unsigned f = 0; f < FL_FEATURES; f++) {
            err = fmaxf(err, fabsf(a.c[c].mean[f] - all.c[c].mean[f]));
            err = fmaxf(err, fabsf(_var(&a.c[c], f) - _var(&all.c[c], f)));
        }
    }
    printf("merge: largest difference %.2g\n", err);
    _check(err < 1e-4f, "merged mean and variance");
}

static void _test_message(void)
{
    fl_stats_t s, d;
    fl_rng_t rng;
    float x[FL_FEATURES];
    uint8_t buf[FL_MSG_LEN];
    uint16_t origin, seq;
    uint8_t hops;

    fl_stats_reset(&s);
    fl_rng_seed(&rng, 2);
    for (unsigned i = 0; i < 50; i++) {
        fl_draw(&rng, 1, x);
        fl_stats_add(&s, 1, x);
        fl_draw(&rng, 3, x);
        fl_stats_add(&s, 3, x);
    }
    size_t len = fl_encode(buf, 0xbeef, 0xfffe, 3, &s);
    _check(len == FL_MSG_LEN, "message length");
    _check(fl_decode(buf, len, &origin, &seq, &hops, &d) == 0, "decode");
    _check(origin == 0xbeef && seq == 0xfffe && hops == 3, "message header");
    float err = 0;
    for (unsigned c = 0; c < FL_CLASSES; c++) {
        _check(d.c[c].n == s.c[c].n, "message count");
        for (unsigned f = 0; f < FL_FEATURES; f++) {
            err = fmaxf(err, fabsf(d.c[c].mean[f] - s.c[c].mean[f]));
            err = fmaxf(err, fabsf(sqrtf(_var(&d.c[c], f)) - sqrtf(_var(&s.c[c], f))));
        }
    }
    printf("message: %zu bytes, largest difference %.4f\n", len, err);
    _check(err <= 1.0f / 256, "message precision");
    buf[0] ^= 1;
    _check(fl_decode(buf, len, &origin, &seq, &hops, &d) != 0, "bad magic rejected");
    _check(fl_decode(buf, len - 1, &origin, &seq, &hops, &d) != 0, "short message rejected");
}

static void _test_table(void)
{
    static fl_table_t t;
    fl_stats_t s;

    fl_table_reset(&t);
    fl_stats_reset(&s);
    _check(fl_table_update(&t, 7, 0xfffe, 2, &s), "new node");
    _check(!fl_table_update(&t, 7, 0xfffe, 2, &s), "same statistics");
    _check(fl_table_update(&t, 7, 0xfffe, 1, &s), "shorter path");
    _check(fl_table_update(&t, 7, 0x0001, 1, &s), "sequence wrap");
    _check(!fl_table_update(&t, 7, 0xffff, 1, &s), "older statistics");
    _check(fl_table_count(&t) == 1, "one node");
    for (unsigned i = 0; i < FL_MAX_NODES + 4; i++) {
        fl_table_update(&t, (uint16_t)(100 + i), 1, 1, &s);
    }
    _check(fl_table_count(&t) == FL_MAX_NODES, "full table");
}

static void _test_federation(void)
{
    static fl_stats_t local[NODES], all, fed;
    static fl_table_t table;
    fl_rng_t rng;
    float x[FL_FEATURES];
    float local_acc = 0;

    fl_stats_reset(&all);
    fl_table_reset(&table);
    for (unsigned n = 0; n < NODES; n++) {
        unsigned classes[2];
        uint16_t id = (uint16_t)(0x1234 + 37 * n);
        fl_node_classes(id, classes);
        _check(classes[0] != classes[1], "two different classes");
        fl_stats_reset(&local[n]);
        fl_rng_seed(&rng, id);
        for (unsigned i = 0; i < SAMPLES; i++) {
            unsigned c = classes[i % 2];
            fl_draw(&rng, c, x);
            fl_stats_add(&local[n], c, x);
            fl_stats_add(&all, c, x);
        }
        fl_model_build(FL_LOCAL, &local[n]);
        local_acc += fl_model_accuracy(FL_LOCAL) / NODES;
        if (n > 0) {
            fl_table_update(&table, id, 1, 1, &local[n]);
        }
    }
    fl_table_merge(&table, 0x1234, &local[0], &fed);
    fl_model_build(FL_FEDERATED, &fed);
    float fed_acc = fl_model_accuracy(FL_FEDERATED);
    fl_model_build(FL_LOCAL, &all);
    float central_acc = fl_model_accuracy(FL_LOCAL);
    printf("federation of %u nodes: local %.1f%%, federated %.1f%%, all samples in one "
           "place %.1f%%\n", NODES, 100 * local_acc, 100 * fed_acc, 100 * central_acc);
    _check(local_acc < 0.55f, "local models see half of the classes");
    _check(fed_acc > 0.8f, "federated model");
    _check(fabsf(fed_acc - central_acc) <= 0.01f, "federated as good as central");
}

static void _test_relay(void)
{
    static fl_table_t tables[LINE];
    static fl_stats_t own[LINE];
    unsigned next[LINE] = { 0 };
    uint8_t buf[FL_MSG_LEN];

    for (unsigned n = 0; n < LINE; n++) {
        fl_table_reset(&tables[n]);
        fl_stats_reset(&own[n]);
        own[n].c[0].n = n + 1;
    }
    /* each round, each node sends its own statistics and relays one entry
       to its two neighbours on the line */
    for (unsigned round = 0; round < 40; round++) {
        for (unsigned n = 0; n < LINE; n++) {
            for (unsigned k = 0; k < 2; k++) {
                uint16_t origin = (uint16_t)n, seq = 1;
                uint8_t hops = 0;
                fl_stats_t s = own[n];
                if (k == 1) {
                    const fl_entry_t *e = NULL;
                    for (unsigned tries = 0; tries < FL_MAX_NODES && !e; tries++) {
                        const fl_entry_t *c = &tables[n].e[next[n]];
                        next[n] = (next[n] + 1) % FL_MAX_NODES;
                        if (c->used && c->hops < FL_MAX_HOPS) {
                            e = c;
                        }
                    }
                    if (!e) {
                        continue;
                    }
                    origin = e->origin;
                    seq = e->seq;
                    hops = e->hops;
                    s = e->stats;
                }
                size_t len = fl_encode(buf, origin, seq, hops, &s);
                for (int d = -1; d <= 1; d += 2) {
                    int m = (int)n + d;
                    fl_stats_t got;
                    if (m < 0 || m >= (int)LINE ||
                        fl_decode(buf, len, &origin, &seq, &hops, &got) != 0 ||
                        origin == m) {
                        continue;
                    }
                    fl_table_update(&tables[m], origin, seq, (uint8_t)(hops + 1), &got);
                }
            }
        }
    }
    unsigned known = fl_table_count(&tables[0]);
    printf("relay on a line of %u nodes: the first node knows %u others\n", LINE, known);
    _check(known == FL_MAX_HOPS, "relayed up to FL_MAX_HOPS hops");
    unsigned middle = fl_table_count(&tables[LINE / 2]);
    _check(middle == LINE - 1, "the middle node knows all the others");
}

int main(void)
{
    _test_merge();
    _test_message();
    _test_table();
    _test_federation();
    _test_relay();
    puts(_failed ? "FAIL" : "PASS");
    return _failed;
}
