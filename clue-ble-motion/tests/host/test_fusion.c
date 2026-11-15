/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the orientation filter. The gradient of the filter is
 * checked against a numerical one, then the board is simulated: a known
 * rotation gives the gyroscope (with a bias and noise), the accelerometer
 * and the magnetometer readings, and the filter must follow it.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* the static functions too */
#include "../../fusion.c"

#define RATE        (100.0f)
#define DT          (1.0f / RATE)

static int _failures;

static void _check(const char *what, int ok)
{
    printf("%s: %s\n", what, ok ? "ok" : "FAIL");
    _failures += !ok;
}

static float _gauss(void)
{
    float u = (rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    float v = (rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    return sqrtf(-2 * logf(u)) * cosf(2 * (float)M_PI * v);
}

/* v in the sensor frame from v in the earth frame: q* v q */
static void _to_sensor(const float q[4], const float v[3], float out[3])
{
    const float qc[4] = { q[0], -q[1], -q[2], -q[3] };
    const float vq[4] = { 0, v[0], v[1], v[2] };
    float t[4], r[4];

    fusion_mul(qc, vq, t);
    fusion_mul(t, q, r);
    out[0] = r[1];
    out[1] = r[2];
    out[2] = r[3];
}

/* angle between two orientations, in degrees */
static float _angle(const float a[4], const float b[4])
{
    float dot = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2 * acosf(dot > 1 ? 1 : dot) / DEG;
}

/* angle between the vertical axes seen by two orientations, in degrees */
static float _tilt_error(const float a[4], const float b[4])
{
    static const float up[3] = { 0, 0, 1 };
    float ua[3], ub[3];

    _to_sensor(a, up, ua);
    _to_sensor(b, up, ub);
    float dot = ua[0] * ub[0] + ua[1] * ub[1] + ua[2] * ub[2];
    return acosf(dot > 1 ? 1 : (dot < -1 ? -1 : dot)) / DEG;
}

/* the true orientation turns by the body rate w (degrees per second) */
static void _turn(float q[4], const float w[3], float dt)
{
    float angle = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * DEG * dt;
    if (angle <= 0) {
        return;
    }
    float s = sinf(angle / 2) / (angle / dt);
    const float r[4] = { cosf(angle / 2), w[0] * DEG * s, w[1] * DEG * s, w[2] * DEG * s };
    fusion_mul(q, r, q);
    _normalize(q, 4);
}

/* objective of the filter, with the reference field (bx, 0, bz) fixed */
static float _cost(const float q[4], const float a[3], const float m[3], float bx, float bz)
{
    float f[6] = {
        2 * (q[1] * q[3] - q[0] * q[2]) - a[0],
        2 * (q[0] * q[1] + q[2] * q[3]) - a[1],
        2 * (0.5f - q[1] * q[1] - q[2] * q[2]) - a[2],
        2 * bx * (0.5f - q[2] * q[2] - q[3] * q[3]) + 2 * bz * (q[1] * q[3] - q[0] * q[2]) - m[0],
        2 * bx * (q[1] * q[2] - q[0] * q[3]) + 2 * bz * (q[0] * q[1] + q[2] * q[3]) - m[1],
        2 * bx * (q[0] * q[2] + q[1] * q[3]) + 2 * bz * (0.5f - q[1] * q[1] - q[2] * q[2]) - m[2],
    };
    float c = 0;
    for (unsigned i = 0; i < 6; i++) {
        c += f[i] * f[i];
    }
    return c / 2;
}

static void _test_gradient(void)
{
    float worst = 0;

    for (unsigned t = 0; t < 200; t++) {
        float q[4] = { _gauss(), _gauss(), _gauss(), _gauss() };
        float a[3] = { _gauss(), _gauss(), _gauss() };
        float m[3] = { _gauss(), _gauss(), _gauss() };
        _normalize(q, 4);
        _normalize(a, 3);
        _normalize(m, 3);

        float step[4] = { 0 };
        _gravity_step(q, a, step);
        _mag_step(q, m, step);

        /* the reference field the filter takes from q */
        float qm[4], h[4];
        const float mv[4] = { 0, m[0], m[1], m[2] };
        const float qc[4] = { q[0], -q[1], -q[2], -q[3] };
        fusion_mul(q, mv, qm);
        fusion_mul(qm, qc, h);
        float bx = sqrtf(h[1] * h[1] + h[2] * h[2]);
        float bz = h[3];

        for (unsigned i = 0; i < 4; i++) {
            const float eps = 1e-3f;
            float qp[4] = { q[0], q[1], q[2], q[3] };
            float qn[4] = { q[0], q[1], q[2], q[3] };
            qp[i] += eps;
            qn[i] -= eps;
            float num = (_cost(qp, a, m, bx, bz) - _cost(qn, a, m, bx, bz)) / (2 * eps);
            float err = fabsf(num - step[i]);
            worst = err > worst ? err : worst;
        }
    }
    printf("gradient: worst difference with the numerical one %.5f\n", worst);
    _check("the gradient of the filter is the one of its objective", worst < 1e-2f);
}

static void _test_bias(void)
{
    fusion_bias_t b;
    const float bias[3] = { 1.5f, -2.0f, 0.7f };

    fusion_bias_start(&b);
    bool done = false;
    unsigned samples = 0;
    while (!done && samples < 1000) {
        float dps[3];
        for (unsigned i = 0; i < 3; i++) {
            dps[i] = bias[i] + 0.3f * _gauss();
        }
        /* the board is moved after 1 s: the calibration starts over */
        if (samples == 100) {
            dps[0] += 30;
        }
        done = fusion_bias_add(&b, dps, DT);
        samples++;
    }
    float err = 0;
    for (unsigned i = 0; i < 3; i++) {
        err = fmaxf(err, fabsf(b.bias[i] - bias[i]));
    }
    printf("bias: done after %u samples, error %.3f dps\n", samples, err);
    _check("the gyroscope bias is found, after the board moved", done && err < 0.1f &&
           samples > 300);
}

/* the body rate at time t: turns about each axis, then all of them */
static void _rate(float t, float w[3])
{
    w[0] = w[1] = w[2] = 0;
    if (t < 4) {
        return;
    }
    if (t < 6) {
        w[0] = 90;
    }
    else if (t < 8) {
        w[1] = -90;
    }
    else if (t < 10) {
        w[2] = 120;
    }
    else if (t < 20) {
        w[0] = 150 * sinf(1.3f * t);
        w[1] = 120 * sinf(0.9f * t + 1);
        w[2] = 100 * sinf(0.7f * t + 2);
    }
}

/* simulate 25 s, return the worst errors after the start */
static void _simulate(bool use_mag, float *tilt_worst, float *angle_end)
{
    static const float up[3] = { 0, 0, 1 };
    /* the field points north and down, as in Europe */
    static const float north[3] = { 0.45f, 0, -0.89f };
    const float bias[3] = { 1.2f, -0.8f, 0.5f };
    float truth[4] = { 0.9f, 0.3f, -0.2f, 0.25f };
    fusion_t f;
    fusion_bias_t b;
    /* the true orientation with its own heading reference */
    fusion_t ref = { 0 };
    bool centered = false;

    srand(7);
    _normalize(truth, 4);
    fusion_init(&f);
    fusion_bias_start(&b);
    *tilt_worst = 0;

    for (float t = 0; t < 25; t += DT) {
        float w[3], dps[3], g[3], m[3];
        _rate(t, w);
        _turn(truth, w, DT);
        _to_sensor(truth, up, g);
        _to_sensor(truth, north, m);
        for (unsigned i = 0; i < 3; i++) {
            dps[i] = w[i] + bias[i] + 0.2f * _gauss();
            g[i] += 0.01f * _gauss();
            m[i] += 0.02f * _gauss();
        }
        if (b.running) {
            fusion_bias_add(&b, dps, DT);
        }
        for (unsigned i = 0; i < 3; i++) {
            dps[i] -= b.running ? 0 : b.bias[i];
        }
        fusion_update(&f, dps, g, use_mag ? m : NULL, DT);
        if (t > 3) {
            *tilt_worst = fmaxf(*tilt_worst, _tilt_error(f.q, truth));
            if (!centered) {
                /* both recentered once, after the start: the heading
                   drifts from there on */
                memcpy(ref.q, truth, sizeof(truth));
                fusion_recenter(&ref);
                fusion_recenter(&f);
                centered = true;
            }
        }
    }
    if (use_mag) {
        *angle_end = _angle(f.q, truth);
    }
    else {
        /* without north, the heading is relative to the one at 3 s */
        float rel_f[4], rel_t[4];
        memcpy(ref.q, truth, sizeof(truth));
        fusion_relative(&f, rel_f);
        fusion_relative(&ref, rel_t);
        *angle_end = _angle(rel_f, rel_t);
    }
}

static void _test_motion(void)
{
    float tilt, angle;

    _simulate(false, &tilt, &angle);
    printf("6 axis: worst tilt error %.1f deg, %.1f deg from the board 22 s after "
           "recentering\n", tilt, angle);
    _check("6 axis: the tilt follows the board within 5 degrees", tilt < 5);
    _check("6 axis: the heading drifts less than 5 degrees in 22 s", angle < 5);

    _simulate(true, &tilt, &angle);
    printf("9 axis: worst tilt error %.1f deg, %.1f deg from the board at the end\n", tilt, angle);
    _check("9 axis: the tilt follows the board within 5 degrees", tilt < 5);
    _check("9 axis: the heading too, within 3 degrees", angle < 3);
}

static void _test_recenter(void)
{
    fusion_t f;
    float q[4], roll, pitch, yaw;

    fusion_init(&f);
    /* tilted by 20 degrees about x, then turned by 70 degrees about z */
    const float tilt[4] = { cosf(10 * DEG), sinf(10 * DEG), 0, 0 };
    const float turn[4] = { cosf(35 * DEG), 0, 0, sinf(35 * DEG) };
    fusion_mul(turn, tilt, f.q);
    fusion_recenter(&f);
    fusion_relative(&f, q);
    fusion_euler(q, &roll, &pitch, &yaw);
    printf("recentered: roll %.1f pitch %.1f yaw %.1f\n", roll, pitch, yaw);
    _check("recentering keeps the tilt and zeroes the heading",
           fabsf(roll - 20) < 0.1f && fabsf(pitch) < 0.1f && fabsf(yaw) < 0.1f);
}

int main(void)
{
    srand(1);
    _test_gradient();
    _test_bias();
    _test_motion();
    _test_recenter();
    puts(_failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
