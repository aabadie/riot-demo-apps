/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     demo_tone
 * @{
 *
 * @file
 * @brief       Square wave tones and melodies on a GPIO
 *
 * A ZTIMER_USEC timer toggles the pin every half period, a ZTIMER_MSEC timer
 * steps through the notes. Both run in interrupt context.
 *
 * @}
 */

#include "irq.h"
#include "ztimer.h"
#include "demo_tone.h"

typedef struct {
    const tone_note_t *notes;
    size_t len;
    size_t idx;
    bool loop;
} _track_t;

static gpio_t _pin = GPIO_UNDEF;
static ztimer_t _wave_timer;
static ztimer_t _note_timer;
static uint32_t _half_period_us;
static _track_t _music;
static _track_t _fx;
static bool _muted;

static void _wave_cb(void *arg)
{
    (void)arg;
    if (_half_period_us) {
        gpio_toggle(_pin);
        ztimer_set(ZTIMER_USEC, &_wave_timer, _half_period_us);
    }
}

static void _set_freq(uint16_t freq)
{
    ztimer_remove(ZTIMER_USEC, &_wave_timer);
    if (!gpio_is_valid(_pin)) {
        _half_period_us = 0;
        return;
    }
    gpio_clear(_pin);
    if (freq == NOTE_REST || _muted) {
        _half_period_us = 0;
        return;
    }
    _half_period_us = 500000UL / freq;
    ztimer_set(ZTIMER_USEC, &_wave_timer, _half_period_us);
}

/* Play the current note of the active track and schedule the next one */
static void _step(void)
{
    _track_t *track = _fx.notes ? &_fx : &_music;

    if (track->notes == NULL) {
        _set_freq(NOTE_REST);
        return;
    }

    if (track->idx >= track->len) {
        if (track->loop) {
            track->idx = 0;
        }
        else {
            track->notes = NULL;
            /* back to the melody after an effect */
            _step();
            return;
        }
    }

    const tone_note_t *note = &track->notes[track->idx++];
    _set_freq(note->freq);
    ztimer_set(ZTIMER_MSEC, &_note_timer, note->ms);
}

static void _note_cb(void *arg)
{
    (void)arg;
    _step();
}

void tone_init(gpio_t pin)
{
    _pin = pin;
    if (gpio_is_valid(_pin)) {
        gpio_init(_pin, GPIO_OUT);
        gpio_clear(_pin);
    }
    _wave_timer.callback = _wave_cb;
    _note_timer.callback = _note_cb;
}

void tone_play(const tone_note_t *notes, size_t len, bool loop)
{
    unsigned state = irq_disable();
    _music.notes = notes;
    _music.len = len;
    _music.idx = 0;
    _music.loop = loop;
    if (_fx.notes == NULL) {
        ztimer_remove(ZTIMER_MSEC, &_note_timer);
        _step();
    }
    irq_restore(state);
}

void tone_stop(void)
{
    unsigned state = irq_disable();
    _music.notes = NULL;
    if (_fx.notes == NULL) {
        ztimer_remove(ZTIMER_MSEC, &_note_timer);
        _set_freq(NOTE_REST);
    }
    irq_restore(state);
}

void tone_effect(const tone_note_t *notes, size_t len)
{
    unsigned state = irq_disable();
    ztimer_remove(ZTIMER_MSEC, &_note_timer);
    /* a melody note cut by the effect is replayed from the next note */
    _fx.notes = notes;
    _fx.len = len;
    _fx.idx = 0;
    _fx.loop = false;
    _step();
    irq_restore(state);
}

void tone_beep(uint16_t freq, uint16_t ms)
{
    static tone_note_t beep;
    unsigned state = irq_disable();
    beep.freq = freq;
    beep.ms = ms;
    tone_effect(&beep, 1);
    irq_restore(state);
}

void tone_mute(bool mute)
{
    unsigned state = irq_disable();
    _muted = mute;
    if (mute) {
        _set_freq(NOTE_REST);
    }
    irq_restore(state);
}

bool tone_is_muted(void)
{
    return _muted;
}
