/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Interference detection on the 16 channels of IEEE 802.15.4
 */

#include <math.h>
#include <string.h>

#include "detector.h"

/* the emlearn headers define functions, only include them here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "eml_mixture.h"
#pragma GCC diagnostic pop

#ifndef M_PI
#define M_PI        (3.14159265358979323846)
#endif

#define K           DET_COMPONENTS
#define F           DET_CHANNELS
#define EM_ITER     (40)
#define VAR_MIN     (0.002f)    /* occupancy std of 0.045 at least */
#define STD_MIN     (0.05f)     /* for the channel z scores */

static float _train[DET_TRAIN_MAX][F];
static unsigned _n_train;
static float _mean_all[F];
static float _std_all[F];

static float _means[K * F];
static float _precisions[K * F * F];
static float _log_dets[K];
static float _log_weights[K];
static EmlMixtureModel _model = {
    K, F, EmlCovarianceFull, _means, _precisions, _log_dets, _log_weights,
};
static bool _trained;
static float _ll_median, _ll_spread;

void det_learn_reset(void)
{
    _n_train = 0;
    _trained = false;
}

unsigned det_learn_add(const float occupancy[DET_CHANNELS])
{
    if (_n_train < DET_TRAIN_MAX) {
        memcpy(_train[_n_train++], occupancy, sizeof(float) * F);
    }
    return _n_train;
}

bool det_trained(void)
{
    return _trained;
}

float det_learned_mean(unsigned channel)
{
    return channel < F ? _mean_all[channel] : 0;
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

int det_learn_fit(void)
{
    const unsigned n = _n_train;
    static float resp[DET_TRAIN_MAX][K];
    float var[K][F], weight[K], global_var[F];

    if (n < 4 * K) {
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
        _std_all[f] = sqrtf(v / n);
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
        for (unsigned i = 0; i < n; i++) {
            float lp[K], total;
            for (unsigned k = 0; k < K; k++) {
                float s = logf(weight[k] + 1e-12f);
                for (unsigned f = 0; f < F; f++) {
                    float d = _train[i][f] - _means[k * F + f];
                    s -= 0.5f * (logf(2.0f * (float)M_PI * var[k][f]) +
                                 d * d / var[k][f]);
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

    /* spread of the log-likelihood of the learned scans */
    static float ll[DET_TRAIN_MAX];
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

/* The longest run of adjacent channels well above what was learned */
static void _channels(const float occupancy[DET_CHANNELS], det_result_t *result)
{
    unsigned best_first = 0, best_len = 0;
    unsigned run_first = 0, run_len = 0;

    for (unsigned f = 0; f < F; f++) {
        float std = _std_all[f] < STD_MIN ? STD_MIN : _std_all[f];
        bool hot = (occupancy[f] - _mean_all[f]) / std >= DET_CHANNEL_Z;
        if (hot) {
            if (run_len == 0) {
                run_first = f;
            }
            run_len++;
            if (run_len > best_len) {
                best_len = run_len;
                best_first = run_first;
            }
        }
        else {
            run_len = 0;
        }
    }
    if (best_len == 0) {
        result->kind = DET_UNUSUAL;
        return;
    }
    result->kind = best_len >= 3 ? DET_WIDEBAND : DET_NARROWBAND;
    result->first = (uint8_t)(DET_FIRST_CHANNEL + best_first);
    result->last = (uint8_t)(DET_FIRST_CHANNEL + best_first + best_len - 1);
}

void det_score(const float occupancy[DET_CHANNELS], det_result_t *result)
{
    float proba[K], ll = 0;

    memset(result, 0, sizeof(*result));
    if (!_trained) {
        return;
    }
    eml_mixture_score(&_model, occupancy, F, proba, &ll);
    float level = (_ll_median - ll) / _ll_spread;
    result->level = level < 0 ? 0 : level;
    if (result->level >= DET_ALARM_LEVEL) {
        _channels(occupancy, result);
    }
}

const char *det_kind_name(det_kind_t kind)
{
    switch (kind) {
    case DET_NARROWBAND:
        return "narrowband";
    case DET_WIDEBAND:
        return "wideband";
    case DET_UNUSUAL:
        return "unusual";
    default:
        return "none";
    }
}
