/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Pong rules and the neural network opponent, plain C without
 *              RIOT dependencies: tools/train.py runs the same game through
 *              ctypes to train the network
 *
 * The player is on the left, the bot on the right. Positions are in field
 * pixels, speeds in pixels per frame.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PONG_W          (160)   /**< field width */
#define PONG_H          (112)   /**< field height */
#define PONG_PADDLE_H   (22)    /**< paddle height */
#define PONG_PADDLE_W   (3)     /**< paddle width */
#define PONG_PADDLE_X   (6)     /**< paddle distance to the side */
#define PONG_BALL       (4)     /**< ball size */
#define PONG_SPEED      (2.4f)  /**< speed of the player paddle */
#define PONG_FEATURES   (7)     /**< inputs of the network */
#define PONG_ACTIONS    (3)     /**< up, stay, down */

/**
 * @brief   Paddle moves
 */
enum {
    PONG_UP = -1,
    PONG_STAY = 0,
    PONG_DOWN = 1,
};

/**
 * @brief   Game state
 */
typedef struct {
    float bx, by;       /**< ball, top left corner */
    float vx, vy;       /**< ball speed */
    float left, right;  /**< paddles, top */
    float bot_speed;    /**< speed of the bot paddle */
    unsigned hits;      /**< hits of the current rally */
    uint32_t rng;       /**< state of the serve random generator */
} pong_t;

/**
 * @brief   Start a game, the serve goes to a random side
 */
void pong_init(pong_t *game, uint32_t seed, float bot_speed);

/**
 * @brief   Serve again from the center
 *
 * @param[in] towards   -1 towards the player, 1 towards the bot
 */
void pong_serve(pong_t *game, int towards);

/**
 * @brief   Play a frame
 *
 * @return  0 while the rally goes on, -1 when the bot scores, 1 when the
 *          player scores
 */
int pong_step(pong_t *game, int left, int right);

/**
 * @brief   Whether the last pong_step made a paddle hit the ball
 */
int pong_hit(const pong_t *game);

/**
 * @brief   Inputs of the network, seen from the bot
 */
void pong_features(const pong_t *game, float features[PONG_FEATURES]);

/**
 * @brief   Scripted player, which tools/train.py teaches to the network
 *
 * Follows the ball, and when it comes, goes where it will arrive, with the
 * bounces on the walls.
 *
 * @param[in] side  -1 for the left paddle, 1 for the right one
 */
int pong_expert(const pong_t *game, int side);

/**
 * @brief   Move of the neural network for the right paddle
 *
 * @param[out] proba    probability of up, stay and down, can be NULL
 */
int pong_bot(const pong_t *game, float proba[PONG_ACTIONS]);

/**
 * @brief   Activations of the hidden neurons after the last pong_bot
 *
 * @return  number of hidden neurons
 */
unsigned pong_bot_hidden(const float **values);

#ifdef __cplusplus
}
#endif
