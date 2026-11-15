/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Vibration anomaly detection, Gaussian mixture fitted on the
 *              device and scored by emlearn
 */

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "vibe.h"

/* the emlearn headers define functions, only include them here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "eml_fft.h"
#include "eml_mixture.h"
#pragma GCC diagnostic pop

#define K           VIBE_COMPONENTS
#define F           VIBE_BANDS
#define EM_ITER     (50)
#define VAR_MIN     (0.25f)     /* dB^2, keeps components from collapsing */
#define POWER_MIN   (1e-12f)

const float vibe_band_edges[VIBE_BANDS + 1] = {
    2, 4, 7, 11, 17, 26, 40, 62, 100,
};

static float _fft_sin[VIBE_WINDOW / 2];
static float _fft_cos[VIBE_WINDOW / 2];
static EmlFFT _fft = { VIBE_WINDOW / 2, _fft_sin, _fft_cos };
static float _hann[VIBE_WINDOW];

static float _train[VIBE_TRAIN_MAX][F];
static unsigned _n_train;
static float _mean_all[F];

static float _means[K * F];
static float _precisions[K * F * F];
static float _log_dets[K];
static float _log_weights[K];
static EmlMixtureModel _model = {
    K, F, EmlCovarianceFull, _means, _precisions, _log_dets, _log_weights,
};
static bool _trained;
static float _ll_median, _ll_spread;

void vibe_init(void)
{
    eml_fft_fill(_fft, VIBE_WINDOW);
    for (unsigned i = 0; i < VIBE_WINDOW; i++) {
        _hann[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (VIBE_WINDOW - 1));
    }
}

void vibe_features(const float window[VIBE_WINDOW][3],
                   float features[VIBE_BANDS])
{
    static float re[VIBE_WINDOW], im[VIBE_WINDOW];
    float power[VIBE_WINDOW / 2];

    memset(power, 0, sizeof(power));
    for (unsigned axis = 0; axis < 3; axis++) {
        /* remove gravity and the slow drift with the mean of the window */
        float mean = 0;
        for (unsigned i = 0; i < VIBE_WINDOW; i++) {
            mean += window[i][axis];
        }
        mean /= VIBE_WINDOW;
        for (unsigned i = 0; i < VIBE_WINDOW; i++) {
            re[i] = (window[i][axis] - mean) * _hann[i];
            im[i] = 0;
        }
        eml_fft_forward(_fft, re, im, VIBE_WINDOW);
        for (unsigned i = 0; i < VIBE_WINDOW / 2; i++) {
            power[i] += re[i] * re[i] + im[i] * im[i];
        }
    }

    const float bin_hz = (float)VIBE_RATE_HZ / VIBE_WINDOW;
    for (unsigned b = 0; b < VIBE_BANDS; b++) {
        unsigned lo = (unsigned)ceilf(vibe_band_edges[b] / bin_hz);
        unsigned hi = (unsigned)ceilf(vibe_band_edges[b + 1] / bin_hz);
        float sum = 0;
        hi = hi > VIBE_WINDOW / 2 ? VIBE_WINDOW / 2 : hi;
        for (unsigned i = lo; i < hi; i++) {
            sum += power[i];
        }
        features[b] = 10.0f * log10f(sum / VIBE_WINDOW + POWER_MIN);
    }
}

void vibe_learn_reset(void)
{
    _n_train = 0;
    _trained = false;
}

unsigned vibe_learn_add(const float features[VIBE_BANDS])
{
    if (_n_train < VIBE_TRAIN_MAX) {
        memcpy(_train[_n_train++], features, sizeof(float) * F);
    }
    return _n_train;
}

bool vibe_trained(void)
{
    return _trained;
}

const float *vibe_learned_mean(void)
{
    return _mean_all;
}

static float _dist2(const float *a, const float *b)
{
    float d = 0;

    for (unsigned f = 0; f < F; f++) {
        d += (a[f] - b[f]) * (a[f] - b[f]);
    }
    return d;
}

static void _sort(float *v, unsigned n)
{
    for (unsigned i = 1; i < n; i++) {
        float x = v[i];
        unsigned j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
}

int vibe_learn_fit(void)
{
    const unsigned n = _n_train;
    static float resp[VIBE_TRAIN_MAX][K];
    float var[K][F], weight[K], global_var[F];

    if (n < 2 * K) {
        return -1;
    }

    /* global statistics */
    for (unsigned f = 0; f < F; f++) {
        float m = 0, v = 0;
        for (unsigned i = 0; i < n; i++) {
            m += _train[i][f];
        }
        m /= n;
        for (unsigned i = 0; i < n; i++) {
            v += (_train[i][f] - m) * (_train[i][f] - m);
        }
        _mean_all[f] = m;
        global_var[f] = v / n + VAR_MIN;
    }

    /* initial means: farthest point sampling, deterministic */
    unsigned first = 0;
    float best = -1;
    for (unsigned i = 0; i < n; i++) {
        float d = _dist2(_train[i], _mean_all);
        if (best < 0 || d < best) {
            best = d;
            first = i;
        }
    }
    memcpy(&_means[0], _train[first], sizeof(float) * F);
    for (unsigned k = 1; k < K; k++) {
        unsigned far = 0;
        best = -1;
        for (unsigned i = 0; i < n; i++) {
            float d = INFINITY;
            for (unsigned j = 0; j < k; j++) {
                float dj = _dist2(_train[i], &_means[j * F]);
                d = dj < d ? dj : d;
            }
            if (d > best) {
                best = d;
                far = i;
            }
        }
        memcpy(&_means[k * F], _train[far], sizeof(float) * F);
    }
    for (unsigned k = 0; k < K; k++) {
        memcpy(var[k], global_var, sizeof(global_var));
        weight[k] = 1.0f / K;
    }

    /* expectation maximization with diagonal covariances */
    for (unsigned iter = 0; iter < EM_ITER; iter++) {
        /* the terms that do not depend on the sample, once per iteration:
           logf is slow in software on the boards without FPU */
        float base[K], inv_var[K][F];
        for (unsigned k = 0; k < K; k++) {
            base[k] = logf(weight[k] + 1e-12f);
            for (unsigned f = 0; f < F; f++) {
                base[k] -= 0.5f * logf(2.0f * (float)M_PI * var[k][f]);
                inv_var[k][f] = 1.0f / var[k][f];
            }
        }
        for (unsigned i = 0; i < n; i++) {
            float lp[K], total;
            for (unsigned k = 0; k < K; k++) {
                float s = base[k];
                for (unsigned f = 0; f < F; f++) {
                    float d = _train[i][f] - _means[k * F + f];
                    s -= 0.5f * d * d * inv_var[k][f];
                }
                lp[k] = s;
            }
            eml_logsumexp(lp, K, &total);
            for (unsigned k = 0; k < K; k++) {
                resp[i][k] = expf(lp[k] - total);
            }
        }
        for (unsigned k = 0; k < K; k++) {
            float nk = 1e-9f;
            for (unsigned i = 0; i < n; i++) {
                nk += resp[i][k];
            }
            weight[k] = nk / n;
            for (unsigned f = 0; f < F; f++) {
                float m = 0, v = 0;
                for (unsigned i = 0; i < n; i++) {
                    m += resp[i][k] * _train[i][f];
                }
                m /= nk;
                for (unsigned i = 0; i < n; i++) {
                    float d = _train[i][f] - m;
                    v += resp[i][k] * d * d;
                }
                _means[k * F + f] = m;
                var[k][f] = v / nk + VAR_MIN;
            }
        }
    }

    /* emlearn wants the Cholesky factor of the precision matrix, diagonal
       here, and its log determinant */
    memset(_precisions, 0, sizeof(_precisions));
    for (unsigned k = 0; k < K; k++) {
        float log_det = 0;
        for (unsigned f = 0; f < F; f++) {
            float p = 1.0f / sqrtf(var[k][f]);
            _precisions[k * F * F + f * F + f] = p;
            log_det += logf(p);
        }
        _log_dets[k] = log_det;
        _log_weights[k] = logf(weight[k] + 1e-12f);
    }

    /* spread of the log-likelihood of the learned windows */
    static float ll[VIBE_TRAIN_MAX];
    float proba[K];
    for (unsigned i = 0; i < n; i++) {
        eml_mixture_score(&_model, _train[i], F, proba, &ll[i]);
    }
    _sort(ll, n);
    _ll_median = ll[n / 2];
    _ll_spread = _ll_median - ll[n / 20];
    _ll_spread = _ll_spread < 1.0f ? 1.0f : _ll_spread;
    _trained = true;
    return 0;
}

float vibe_level(const float features[VIBE_BANDS], float *log_lik)
{
    float proba[K], ll = 0;

    if (!_trained) {
        return 0;
    }
    eml_mixture_score(&_model, features, F, proba, &ll);
    if (log_lik) {
        *log_lik = ll;
    }
    float level = (_ll_median - ll) / _ll_spread;
    return level < 0 ? 0 : level;
}
