/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Neural network opponent, the emlearn MLP of model.h trained
 *              by tools/train.py
 */

#include "pong.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
/* the training tools build the C model with the model they just made */
#ifndef MODEL_HEADER
#define MODEL_HEADER "model.h"
#endif
#include MODEL_HEADER
#pragma GCC diagnostic pop

int pong_bot(const pong_t *game, float proba[PONG_ACTIONS])
{
    float features[PONG_FEATURES];
    float p[PONG_ACTIONS];

    pong_features(game, features);
    if (eml_net_predict_proba(&pong_model, features, PONG_FEATURES, p,
                              PONG_ACTIONS) != EmlOk) {
        return PONG_STAY;
    }
    int best = 0;
    for (int i = 1; i < PONG_ACTIONS; i++) {
        if (p[i] > p[best]) {
            best = i;
        }
    }
    if (proba) {
        for (int i = 0; i < PONG_ACTIONS; i++) {
            proba[i] = p[i];
        }
    }
    /* classes 0, 1, 2 are up, stay, down */
    return best - 1;
}

unsigned pong_bot_hidden(const float **values)
{
    /* after an inference, the first buffer holds the hidden layer */
    *values = pong_model.activations1;
    return pong_model.layers[0].n_outputs;
}
