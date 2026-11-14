/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Handwritten digit recognition with an emlearn MLP trained on
 *              MNIST by tools/train.py
 */

#include <math.h>
#include <string.h>

#include "digits.h"
/* the emlearn headers define a table of names that is not used here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "model.h"
#pragma GCC diagnostic pop

#define POOL        (2)
#define FEATURES    ((DIGITS_SIZE / POOL) * (DIGITS_SIZE / POOL))
#define BOX         (20)        /* side of the box the digit is scaled to */
#define INK_MIN     (16)        /* pixels below this are background */

static float _sample(const uint8_t *in, float x, float y)
{
    /* bilinear interpolation, outside pixels are background */
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float fx = x - x0;
    float fy = y - y0;
    float v = 0;

    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            int px = x0 + dx;
            int py = y0 + dy;
            if (px < 0 || py < 0 || px >= DIGITS_SIZE || py >= DIGITS_SIZE) {
                continue;
            }
            float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            v += w * in[py * DIGITS_SIZE + px];
        }
    }
    return v;
}

int digits_normalize(const uint8_t *in, uint8_t *out)
{
    int x_min = DIGITS_SIZE, x_max = -1, y_min = DIGITS_SIZE, y_max = -1;

    for (int y = 0; y < DIGITS_SIZE; y++) {
        for (int x = 0; x < DIGITS_SIZE; x++) {
            if (in[y * DIGITS_SIZE + x] >= INK_MIN) {
                x_min = x < x_min ? x : x_min;
                x_max = x > x_max ? x : x_max;
                y_min = y < y_min ? y : y_min;
                y_max = y > y_max ? y : y_max;
            }
        }
    }
    memset(out, 0, DIGITS_PIXELS);
    if (x_max < 0) {
        return -1;
    }

    /* scale the bounding box so that its longest side is BOX pixels, a thin
       digit like a 1 keeps its aspect ratio */
    int w = x_max - x_min + 1;
    int h = y_max - y_min + 1;
    float scale = (float)BOX / (w > h ? w : h);
    int sw = (int)(w * scale + 0.5f);
    int sh = (int)(h * scale + 0.5f);
    sw = sw < 1 ? 1 : sw;
    sh = sh < 1 ? 1 : sh;

    /* resample the box, centered, then compute its center of mass */
    static uint8_t box[BOX * BOX];
    float mass = 0, mx = 0, my = 0;

    memset(box, 0, sizeof(box));
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            float v;
            if (scale < 1.0f) {
                /* shrinking: average the source area of the pixel */
                float x0 = x_min + x / scale;
                float y0 = y_min + y / scale;
                float step = 1.0f / scale;
                float acc = 0;
                int n = 0;
                for (float sy = y0; sy < y0 + step; sy += 0.5f) {
                    for (float sx = x0; sx < x0 + step; sx += 0.5f) {
                        acc += _sample(in, sx, sy);
                        n++;
                    }
                }
                v = acc / n;
            }
            else {
                v = _sample(in, x_min + (x + 0.5f) / scale - 0.5f,
                            y_min + (y + 0.5f) / scale - 0.5f);
            }
            uint8_t p = v > 255.0f ? 255 : (uint8_t)v;
            box[y * BOX + x] = p;
            mass += p;
            mx += p * x;
            my += p * y;
        }
    }
    if (mass == 0) {
        return -1;
    }

    /* move the center of mass to the center of the image */
    int ox = (int)lroundf(DIGITS_SIZE / 2.0f - mx / mass - 0.5f);
    int oy = (int)lroundf(DIGITS_SIZE / 2.0f - my / mass - 0.5f);

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int tx = x + ox;
            int ty = y + oy;
            if (tx >= 0 && ty >= 0 && tx < DIGITS_SIZE && ty < DIGITS_SIZE) {
                out[ty * DIGITS_SIZE + tx] = box[y * BOX + x];
            }
        }
    }
    return 0;
}

int digits_classify(const uint8_t *image, float proba[DIGITS_CLASSES])
{
    float features[FEATURES];
    const int side = DIGITS_SIZE / POOL;

    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const uint8_t *p = &image[(y * POOL) * DIGITS_SIZE + x * POOL];
            unsigned sum = p[0] + p[1] + p[DIGITS_SIZE] + p[DIGITS_SIZE + 1];
            features[y * side + x] = sum / (POOL * POOL * 255.0f);
        }
    }
    if (eml_net_predict_proba(&digits_model, features, FEATURES, proba,
                              DIGITS_CLASSES) != EmlOk) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < DIGITS_CLASSES; i++) {
        if (proba[i] > proba[best]) {
            best = i;
        }
    }
    return best;
}
