/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Federated learning of a Gaussian naive Bayes classifier
 *
 * Plain C without RIOT dependencies, so it can be tested on the host.
 *
 * A Gaussian naive Bayes model is made of the count, mean and variance of
 * each feature for each class. These statistics of two nodes merge exactly
 * into the statistics of all their samples: sharing them gives every node
 * the model it would have learned from the samples of all the nodes,
 * without sending a single sample.
 *
 * Each node keeps the last statistics heard from each other node, with a
 * sequence number, and relays them to its neighbours for a few hops. Its
 * federated model merges its own statistics with all of them.
 *
 * The samples come from a synthetic dataset of 4 classes, the same on all
 * the nodes; each node only sees 2 of the classes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_CLASSES          (4U)        /**< classes of the dataset */
#define FL_FEATURES         (4U)        /**< features per sample */
#define FL_MAX_NODES        (32U)       /**< other nodes remembered */
#define FL_MAX_HOPS         (4U)        /**< statistics are relayed this far */
#define FL_MSG_LEN          (6U + FL_CLASSES * (2U + 4U * FL_FEATURES)) /**< message size */

/**
 * @brief   Statistics of the samples of one class
 */
typedef struct {
    uint32_t n;                 /**< samples */
    float mean[FL_FEATURES];    /**< mean of each feature */
    float m2[FL_FEATURES];      /**< sum of squared differences to the mean */
} fl_class_stats_t;

/**
 * @brief   Statistics of the samples of all the classes
 */
typedef struct {
    fl_class_stats_t c[FL_CLASSES];     /**< per class */
} fl_stats_t;

/**
 * @brief   Statistics heard from another node
 */
typedef struct {
    bool used;                  /**< slot used */
    uint16_t origin;            /**< node the statistics come from */
    uint16_t seq;               /**< sequence number of the statistics */
    uint8_t hops;               /**< hops from their origin */
    fl_stats_t stats;           /**< the statistics */
} fl_entry_t;

/**
 * @brief   All the statistics heard
 */
typedef struct {
    fl_entry_t e[FL_MAX_NODES];         /**< one per node */
} fl_table_t;

/**
 * @brief   The two models of a node
 */
typedef enum {
    FL_LOCAL,                   /**< from its own samples only */
    FL_FEDERATED,               /**< from the samples of all the nodes heard */
    FL_MODELS,
} fl_model_id_t;

/**
 * @brief   Random numbers for the dataset (xorshift32)
 */
typedef struct {
    uint32_t state;             /**< never 0 */
} fl_rng_t;

/**
 * @brief   Seed a random number generator
 */
void fl_rng_seed(fl_rng_t *rng, uint32_t seed);

/**
 * @brief   Draw a sample of a class of the dataset
 */
void fl_draw(fl_rng_t *rng, unsigned cls, float x[FL_FEATURES]);

/**
 * @brief   The two classes a node sees, from its identifier
 */
void fl_node_classes(uint16_t node, unsigned classes[2]);

/**
 * @brief   Forget all the samples
 */
void fl_stats_reset(fl_stats_t *stats);

/**
 * @brief   Add a sample of class @p cls
 */
void fl_stats_add(fl_stats_t *stats, unsigned cls, const float x[FL_FEATURES]);

/**
 * @brief   Add the statistics @p src to @p dst, as if @p dst had seen the
 *          samples of @p src too
 */
void fl_stats_merge(fl_stats_t *dst, const fl_stats_t *src);

/**
 * @brief   Samples of all the classes
 */
uint32_t fl_stats_count(const fl_stats_t *stats);

/**
 * @brief   Forget all the nodes heard
 */
void fl_table_reset(fl_table_t *table);

/**
 * @brief   Record statistics heard from @p origin
 *
 * @return  true when they are newer than what was known, they are then
 *          worth relaying
 */
bool fl_table_update(fl_table_t *table, uint16_t origin, uint16_t seq, uint8_t hops,
                     const fl_stats_t *stats);

/**
 * @brief   Number of nodes heard
 */
unsigned fl_table_count(const fl_table_t *table);

/**
 * @brief   Merge the own statistics with those of all the nodes heard
 */
void fl_table_merge(const fl_table_t *table, uint16_t own, const fl_stats_t *own_stats,
                    fl_stats_t *out);

/**
 * @brief   Encode statistics into a message of FL_MSG_LEN bytes
 */
size_t fl_encode(uint8_t *buf, uint16_t origin, uint16_t seq, uint8_t hops,
                 const fl_stats_t *stats);

/**
 * @brief   Decode a message
 *
 * @return  0 on success, -1 when it is not a valid message
 */
int fl_decode(const uint8_t *buf, size_t len, uint16_t *origin, uint16_t *seq,
              uint8_t *hops, fl_stats_t *stats);

/**
 * @brief   Build a model with emlearn from statistics, with the classes
 *          that have 2 samples or more
 */
void fl_model_build(fl_model_id_t id, const fl_stats_t *stats);

/**
 * @brief   Class predicted by a model, -1 without a model
 */
int fl_model_predict(fl_model_id_t id, const float x[FL_FEATURES]);

/**
 * @brief   Share of the samples of a fixed test set, of all the classes,
 *          that a model classifies right, 0 to 1
 */
float fl_model_accuracy(fl_model_id_t id);

#ifdef __cplusplus
}
#endif
