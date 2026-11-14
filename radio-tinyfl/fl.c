/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Federated learning of a Gaussian naive Bayes classifier
 */

#include <math.h>
#include <string.h>

#include "fl.h"

/* the emlearn headers define functions, only include them here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "eml_bayes.h"
#pragma GCC diagnostic pop

#define C               FL_CLASSES
#define F               FL_FEATURES
#define MSG_MAGIC       (0x46U)     /* 'F' */
#define MSG_VERSION     (1U)
#define Q               (256.0f)    /* 8.8 fixed point in the messages */
#define STD_MIN         (0.05f)
#define TEST_PER_CLASS  (50U)
#define TEST_SEED       (0x7e57U)

/* centers of the classes: the first two features tell the classes apart,
   the last two only a little */
static const float _centers[C][F] = {
    { 2.0f, 0.0f, 0.5f, 0.0f },
    { 0.0f, 2.0f, -0.5f, 0.3f },
    { -2.0f, 0.0f, 0.5f, -0.3f },
    { 0.0f, -2.0f, -0.5f, 0.0f },
};

typedef struct {
    EmlBayesSummary summaries[C * F];
    EmlBayesModel model;
    int class_of[C];        /* model class to dataset class */
} model_t;

static model_t _models[FL_MODELS];

void fl_rng_seed(fl_rng_t *rng, uint32_t seed)
{
    rng->state = seed ? seed : 0x12345678U;
}

static uint32_t _next(fl_rng_t *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static float _uniform(fl_rng_t *rng)
{
    /* in (0, 1] */
    return ((_next(rng) >> 8) + 1) / 16777216.0f;
}

static float _gauss(fl_rng_t *rng)
{
    float u = _uniform(rng);
    float v = _uniform(rng);
    return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v);
}

void fl_draw(fl_rng_t *rng, unsigned cls, float x[FL_FEATURES])
{
    for (unsigned f = 0; f < F; f++) {
        x[f] = _centers[cls % C][f] + _gauss(rng);
    }
}

void fl_node_classes(uint16_t node, unsigned classes[2])
{
    classes[0] = node % C;
    classes[1] = (classes[0] + 1 + (node / C) % (C - 1)) % C;
}

void fl_stats_reset(fl_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
}

void fl_stats_add(fl_stats_t *stats, unsigned cls, const float x[FL_FEATURES])
{
    fl_class_stats_t *s = &stats->c[cls % C];

    /* Welford's algorithm */
    s->n++;
    for (unsigned f = 0; f < F; f++) {
        float d = x[f] - s->mean[f];
        s->mean[f] += d / s->n;
        s->m2[f] += d * (x[f] - s->mean[f]);
    }
}

void fl_stats_merge(fl_stats_t *dst, const fl_stats_t *src)
{
    for (unsigned c = 0; c < C; c++) {
        fl_class_stats_t *a = &dst->c[c];
        const fl_class_stats_t *b = &src->c[c];
        if (b->n == 0) {
            continue;
        }
        /* parallel variance of Chan, Golub and LeVeque */
        float n = (float)a->n + b->n;
        for (unsigned f = 0; f < F; f++) {
            float d = b->mean[f] - a->mean[f];
            a->mean[f] += d * b->n / n;
            a->m2[f] += b->m2[f] + d * d * (float)a->n * b->n / n;
        }
        a->n += b->n;
    }
}

uint32_t fl_stats_count(const fl_stats_t *stats)
{
    uint32_t n = 0;

    for (unsigned c = 0; c < C; c++) {
        n += stats->c[c].n;
    }
    return n;
}

void fl_table_reset(fl_table_t *table)
{
    memset(table, 0, sizeof(*table));
}

bool fl_table_update(fl_table_t *table, uint16_t origin, uint16_t seq, uint8_t hops,
                     const fl_stats_t *stats)
{
    fl_entry_t *free_slot = NULL;

    for (unsigned i = 0; i < FL_MAX_NODES; i++) {
        fl_entry_t *e = &table->e[i];
        if (!e->used) {
            free_slot = free_slot ? free_slot : e;
            continue;
        }
        if (e->origin != origin) {
            continue;
        }
        if ((int16_t)(seq - e->seq) > 0) {
            e->seq = seq;
            e->hops = hops;
            e->stats = *stats;
            return true;
        }
        /* same statistics through a shorter path: relay them from here */
        if (seq == e->seq && hops < e->hops) {
            e->hops = hops;
            return true;
        }
        return false;
    }
    if (!free_slot) {
        return false;
    }
    free_slot->used = true;
    free_slot->origin = origin;
    free_slot->seq = seq;
    free_slot->hops = hops;
    free_slot->stats = *stats;
    return true;
}

unsigned fl_table_count(const fl_table_t *table)
{
    unsigned n = 0;

    for (unsigned i = 0; i < FL_MAX_NODES; i++) {
        n += table->e[i].used;
    }
    return n;
}

void fl_table_merge(const fl_table_t *table, uint16_t own, const fl_stats_t *own_stats,
                    fl_stats_t *out)
{
    *out = *own_stats;
    for (unsigned i = 0; i < FL_MAX_NODES; i++) {
        const fl_entry_t *e = &table->e[i];
        if (e->used && e->origin != own) {
            fl_stats_merge(out, &e->stats);
        }
    }
}

static void _put16(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)v;
}

static uint16_t _get16(const uint8_t *buf)
{
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

static int16_t _q(float v)
{
    v *= Q;
    v = v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v);
    return (int16_t)lrintf(v);
}

size_t fl_encode(uint8_t *buf, uint16_t origin, uint16_t seq, uint8_t hops,
                 const fl_stats_t *stats)
{
    uint8_t *p = buf;

    *p++ = MSG_MAGIC;
    *p++ = (uint8_t)((MSG_VERSION << 4) | (hops & 0x0f));
    _put16(p, origin);
    p += 2;
    _put16(p, seq);
    p += 2;
    for (unsigned c = 0; c < C; c++) {
        const fl_class_stats_t *s = &stats->c[c];
        _put16(p, s->n > 0xffff ? 0xffff : (uint16_t)s->n);
        p += 2;
        for (unsigned f = 0; f < F; f++) {
            _put16(p, (uint16_t)_q(s->mean[f]));
            p += 2;
        }
        for (unsigned f = 0; f < F; f++) {
            float var = s->n > 1 ? s->m2[f] / (s->n - 1) : 0;
            _put16(p, (uint16_t)_q(sqrtf(var)));
            p += 2;
        }
    }
    return (size_t)(p - buf);
}

int fl_decode(const uint8_t *buf, size_t len, uint16_t *origin, uint16_t *seq,
              uint8_t *hops, fl_stats_t *stats)
{
    const uint8_t *p = buf;

    if (len != FL_MSG_LEN || p[0] != MSG_MAGIC || (p[1] >> 4) != MSG_VERSION) {
        return -1;
    }
    *hops = p[1] & 0x0f;
    *origin = _get16(p + 2);
    *seq = _get16(p + 4);
    p += 6;
    for (unsigned c = 0; c < C; c++) {
        fl_class_stats_t *s = &stats->c[c];
        s->n = _get16(p);
        p += 2;
        for (unsigned f = 0; f < F; f++) {
            s->mean[f] = (int16_t)_get16(p) / Q;
            p += 2;
        }
        for (unsigned f = 0; f < F; f++) {
            float std = (int16_t)_get16(p) / Q;
            if (std < 0) {
                return -1;
            }
            s->m2[f] = s->n > 1 ? std * std * (s->n - 1) : 0;
            p += 2;
        }
    }
    return 0;
}

void fl_model_build(fl_model_id_t id, const fl_stats_t *stats)
{
    model_t *m = &_models[id];
    int classes = 0;

    for (unsigned c = 0; c < C; c++) {
        const fl_class_stats_t *s = &stats->c[c];
        if (s->n < 2) {
            continue;
        }
        for (unsigned f = 0; f < F; f++) {
            float std = sqrtf(s->m2[f] / (s->n - 1));
            std = std < STD_MIN ? STD_MIN : std;
            EmlBayesSummary *sum = &m->summaries[classes * F + f];
            sum->mean = EML_Q16_FROMFLOAT(s->mean[f]);
            sum->std = EML_Q16_FROMFLOAT(std);
            sum->stdlog2 = EML_Q16_FROMFLOAT(log2f(std));
        }
        m->class_of[classes++] = (int)c;
    }
    m->model.n_classes = classes;
    m->model.n_features = F;
    m->model.summaries = m->summaries;
}

int fl_model_predict(fl_model_id_t id, const float x[FL_FEATURES])
{
    model_t *m = &_models[id];

    if (m->model.n_classes == 1) {
        return m->class_of[0];
    }
    if (m->model.n_classes < 2) {
        return -1;
    }
    int32_t c = eml_bayes_predict(&m->model, x, F);
    return c >= 0 ? m->class_of[c] : -1;
}

float fl_model_accuracy(fl_model_id_t id)
{
    fl_rng_t rng;
    float x[F];
    unsigned right = 0;

    fl_rng_seed(&rng, TEST_SEED);
    for (unsigned i = 0; i < TEST_PER_CLASS; i++) {
        for (unsigned c = 0; c < C; c++) {
            fl_draw(&rng, c, x);
            right += fl_model_predict(id, x) == (int)c;
        }
    }
    return (float)right / (TEST_PER_CLASS * C);
}
