/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @file
 * @brief       Sound event detection, features and classification, plain C
 *              without RIOT dependencies: tools/train.py calls the same code
 *              through ctypes
 *
 * The audio is analyzed in frames of SOUND_FRAME samples. A sound event
 * starts when the level rises well above the noise floor and ends when it
 * falls back. The features of the event describe its spectrum and how it
 * evolves, relative to its own loudness.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOUND_RATE          (16000)     /**< sample rate */
#define SOUND_FRAME         (256)       /**< samples per frame, 16 ms */
#define SOUND_BANDS         (16)        /**< bands from 100 Hz to 8 kHz */
#define SOUND_MIN_FRAMES    (1)         /**< shorter events are ignored */
#define SOUND_MAX_FRAMES    (64)        /**< events stop after 1 s */
#define SOUND_START_DB      (12.0f)     /**< start: this far above the floor */
#define SOUND_STOP_DB       (6.0f)      /**< stop: below this... */
#define SOUND_STOP_FRAMES   (6)         /**< ...for that many frames */
#define SOUND_FEATURES      (2 * SOUND_BANDS + 8)   /**< features per event */
#define SOUND_CLASSES       (4)         /**< sounds known by the model */

/**
 * @brief   Names of the sounds, in the order of the classes of the model,
 *          the same as LABELS in tools/sounds.py
 */
#define SOUND_NAMES     { "CLAP", "SNAP", "WHISTLE", "KNOCK" }

/**
 * @brief   Analysis of a frame
 */
typedef struct {
    float band[SOUND_BANDS];    /**< band levels in dB */
    float level;                /**< frame level in dB */
    float centroid;             /**< spectral centroid in Hz */
    float flatness;             /**< 0 for a pure tone, 1 for white noise */
    float peak_hz;              /**< frequency of the strongest bin */
} sound_frame_t;

/**
 * @brief   Detector events
 */
typedef enum {
    SOUND_NONE,     /**< nothing new */
    SOUND_START,    /**< an event started with this frame */
    SOUND_END,      /**< the event is over, see @ref sound_event */
} sound_event_t;

/**
 * @brief   Initialize the FFT tables, the bands and the detector
 */
void sound_init(void);

/**
 * @brief   Analyze a frame of SOUND_FRAME samples
 */
void sound_analyze(const int16_t *pcm, sound_frame_t *out);

/**
 * @brief   Feed the detector with the analysis of the next frame
 */
sound_event_t sound_detect(const sound_frame_t *frame);

/**
 * @brief   Frames of the last event, valid after SOUND_END
 *
 * @param[out] first    index of the first frame of the event, counted from
 *                      the first frame given to the detector
 *
 * @return  number of frames of the event
 */
unsigned sound_event(unsigned *first);

/**
 * @brief   Noise floor estimated by the detector, in dB
 */
float sound_floor(void);

/**
 * @brief   Compute the features of an event from its frames
 *
 * @return  0 on success, -1 when the event is too short
 */
int sound_features(const sound_frame_t *frames, unsigned n, int16_t *features);

/**
 * @brief   Compute the features of an event from its samples, as
 *          tools/train.py does with the recordings of the board
 *
 * @param[in]  pcm      n_frames * SOUND_FRAME samples
 */
int sound_features_pcm(const int16_t *pcm, unsigned n_frames, int16_t *features);

/**
 * @brief   Find the first event of a recording, as the board would, for
 *          tools/train.py: resets the detector
 *
 * @param[out] first    first frame of the event
 *
 * @return  number of frames of the event, 0 without event
 */
unsigned sound_find_event(const int16_t *pcm, unsigned n_frames, unsigned *first);

/**
 * @brief   Classify an event from its features
 *
 * @return  the most likely sound, -1 on error
 */
int sound_classify(const int16_t *features, float *proba);

/**
 * @name    FFT of sound_eml.c, used by sound.c
 * @{
 */
void sound_fft_init(void);
void sound_fft(float *re, float *im);
/** @} */

#ifdef __cplusplus
}
#endif
