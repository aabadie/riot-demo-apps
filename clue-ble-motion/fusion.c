/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Orientation filter of Madgwick, see fusion.h
 *
 * S. O. H. Madgwick, "An efficient orientation filter for inertial and
 * inertial/magnetic sensor arrays", 2010. The gradient of the error is
 * written as J^T f, with the objective f and its Jacobian J.
 */

#include <math.h>
#include <string.h>

#include "fusion.h"

#ifndef M_PI
#define M_PI    (3.14159265358979323846)
#endif

#define DEG     ((float)M_PI / 180.0f)

static void _normalize(float *v, unsigned n)
{
    float norm = 0;

    for (unsigned i = 0; i < n; i++) {
        norm += v[i] * v[i];
    }
    if (norm <= 0) {
        return;
    }
    norm = 1.0f / sqrtf(norm);
    for (unsigned i = 0; i < n; i++) {
        v[i] *= norm;
    }
}

void fusion_mul(const float a[4], const float b[4], float out[4])
{
    float r[4] = {
        a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
        a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
        a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
        a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
    };
    memcpy(out, r, sizeof(r));
}

void fusion_init(fusion_t *f)
{
    static const float identity[4] = { 1, 0, 0, 0 };

    memcpy(f->q, identity, sizeof(identity));
    memcpy(f->ref, identity, sizeof(identity));
    f->time_s = 0;
}

/* add J^T f of the gravity: the earth z axis seen in the sensor frame
   against the measured acceleration a (normalized) */
static void _gravity_step(const float q[4], const float a[3], float step[4])
{
    float f1 = 2 * (q[1] * q[3] - q[0] * q[2]) - a[0];
    float f2 = 2 * (q[0] * q[1] + q[2] * q[3]) - a[1];
    float f3 = 2 * (0.5f - q[1] * q[1] - q[2] * q[2]) - a[2];

    step[0] += -2 * q[2] * f1 + 2 * q[1] * f2;
    step[1] += 2 * q[3] * f1 + 2 * q[0] * f2 - 4 * q[1] * f3;
    step[2] += -2 * q[0] * f1 + 2 * q[3] * f2 - 4 * q[2] * f3;
    step[3] += 2 * q[1] * f1 + 2 * q[2] * f2;
}

/* add J^T f of the magnetic field: the reference field (bx, 0, bz) of the
   earth frame, seen in the sensor frame, against the measured field m
   (normalized) */
static void _mag_step(const float q[4], const float m[3], float step[4])
{
    /* the measured field in the earth frame gives the reference: its
       horizontal part points north */
    float qm[4], h[4];
    const float mv[4] = { 0, m[0], m[1], m[2] };
    const float qc[4] = { q[0], -q[1], -q[2], -q[3] };
    fusion_mul(q, mv, qm);
    fusion_mul(qm, qc, h);
    float bx = sqrtf(h[1] * h[1] + h[2] * h[2]);
    float bz = h[3];

    float f4 = 2 * bx * (0.5f - q[2] * q[2] - q[3] * q[3]) +
               2 * bz * (q[1] * q[3] - q[0] * q[2]) - m[0];
    float f5 = 2 * bx * (q[1] * q[2] - q[0] * q[3]) +
               2 * bz * (q[0] * q[1] + q[2] * q[3]) - m[1];
    float f6 = 2 * bx * (q[0] * q[2] + q[1] * q[3]) +
               2 * bz * (0.5f - q[1] * q[1] - q[2] * q[2]) - m[2];

    step[0] += -2 * bz * q[2] * f4 + (-2 * bx * q[3] + 2 * bz * q[1]) * f5 +
               2 * bx * q[2] * f6;
    step[1] += 2 * bz * q[3] * f4 + (2 * bx * q[2] + 2 * bz * q[0]) * f5 +
               (2 * bx * q[3] - 4 * bz * q[1]) * f6;
    step[2] += (-4 * bx * q[2] - 2 * bz * q[0]) * f4 +
               (2 * bx * q[1] + 2 * bz * q[3]) * f5 +
               (2 * bx * q[0] - 4 * bz * q[2]) * f6;
    step[3] += (-4 * bx * q[3] + 2 * bz * q[1]) * f4 +
               (-2 * bx * q[0] + 2 * bz * q[2]) * f5 + 2 * bx * q[1] * f6;
}

void fusion_update(fusion_t *f, const float dps[3], const float g[3],
                   const float *mag, float dt)
{
    float *q = f->q;
    const float w[4] = { 0, dps[0] * DEG, dps[1] * DEG, dps[2] * DEG };
    float qdot[4];

    /* rate of change from the gyroscope: q' = q w / 2 */
    fusion_mul(q, w, qdot);
    for (unsigned i = 0; i < 4; i++) {
        qdot[i] *= 0.5f;
    }

    float a[3] = { g[0], g[1], g[2] };
    float norm = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    if (norm > 0) {
        float step[4] = { 0 };
        _normalize(a, 3);
        _gravity_step(q, a, step);
        if (mag) {
            float m[3] = { mag[0], mag[1], mag[2] };
            if (m[0] * m[0] + m[1] * m[1] + m[2] * m[2] > 0) {
                _normalize(m, 3);
                _mag_step(q, m, step);
            }
        }
        _normalize(step, 4);
        float beta = f->time_s < FUSION_START_S ? FUSION_BETA_START : FUSION_BETA;
        for (unsigned i = 0; i < 4; i++) {
            qdot[i] -= beta * step[i];
        }
    }

    for (unsigned i = 0; i < 4; i++) {
        q[i] += qdot[i] * dt;
    }
    _normalize(q, 4);
    f->time_s += dt;
}

static float _yaw(const float q[4])
{
    return atan2f(2 * (q[0] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] * q[2] + q[3] * q[3]));
}

void fusion_relative(const fusion_t *f, float q[4])
{
    const float ref_conj[4] = { f->ref[0], -f->ref[1], -f->ref[2], -f->ref[3] };

    fusion_mul(ref_conj, f->q, q);
}

void fusion_recenter(fusion_t *f)
{
    /* only the rotation around the vertical axis of the earth */
    float yaw = _yaw(f->q);

    f->ref[0] = cosf(yaw / 2);
    f->ref[1] = 0;
    f->ref[2] = 0;
    f->ref[3] = sinf(yaw / 2);
}

void fusion_euler(const float q[4], float *roll, float *pitch, float *yaw)
{
    float s = 2 * (q[0] * q[2] - q[3] * q[1]);

    s = s > 1 ? 1 : (s < -1 ? -1 : s);
    *roll = atan2f(2 * (q[0] * q[1] + q[2] * q[3]),
                   1 - 2 * (q[1] * q[1] + q[2] * q[2])) / DEG;
    *pitch = asinf(s) / DEG;
    *yaw = _yaw(q) / DEG;
}

void fusion_bias_start(fusion_bias_t *b)
{
    memset(b->sum, 0, sizeof(b->sum));
    for (unsigned i = 0; i < 3; i++) {
        b->min[i] = INFINITY;
        b->max[i] = -INFINITY;
    }
    b->time_s = 0;
    b->count = 0;
    b->running = true;
}

bool fusion_bias_add(fusion_bias_t *b, const float dps[3], float dt)
{
    if (!b->running) {
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        b->sum[i] += dps[i];
        b->min[i] = fminf(b->min[i], dps[i]);
        b->max[i] = fmaxf(b->max[i], dps[i]);
        if (b->max[i] - b->min[i] > 2 * FUSION_STILL_DPS) {
            /* the board moved, start over */
            fusion_bias_start(b);
            return false;
        }
    }
    b->time_s += dt;
    b->count++;
    if (b->time_s < FUSION_CALIB_S) {
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        b->bias[i] = b->sum[i] / (float)b->count;
    }
    b->running = false;
    return true;
}
