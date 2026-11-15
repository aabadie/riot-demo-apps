/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Host test of the event detector: a clap is one event, a noise that gets
 * 20 dB louder and stays so gives at most one event, and the frames
 * skipped while the board beeps drop the event in progress.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "sound.h"

static int _failures;

static void _check(const char *what, int ok)
{
    printf("%s: %s\n", what, ok ? "ok" : "FAIL");
    _failures += !ok;
}

static float _noise(void)
{
    return 2.0f * rand() / (float)RAND_MAX - 1.0f;
}

/* feed frames of noise of this amplitude, count the events that end */
static unsigned _feed(unsigned frames, float amp, int skip_from)
{
    static int16_t pcm[SOUND_FRAME];
    sound_frame_t f;
    unsigned events = 0;

    for (unsigned n = 0; n < frames; n++) {
        for (unsigned i = 0; i < SOUND_FRAME; i++) {
            pcm[i] = (int16_t)(amp * _noise());
        }
        sound_analyze(pcm, &f);
        if (skip_from >= 0 && n >= (unsigned)skip_from) {
            sound_skip();
            continue;
        }
        events += sound_detect(&f) == SOUND_END;
    }
    return events;
}

int main(void)
{
    srand(1);
    sound_init();
    _feed(100, 30, -1);

    unsigned clap = _feed(4, 3000, -1) + _feed(30, 30, -1);
    _check("a clap is one event", clap == 1);

    unsigned loud = _feed(600, 300, -1);
    printf("%u events in 10 s of steady louder noise\n", loud);
    _check("a louder steady noise gives at most one event", loud <= 1);

    _feed(300, 30, -1);
    unsigned beep = _feed(4, 3000, 2) + _feed(30, 30, -1);
    _check("an event cut by skipped frames is dropped", beep == 0);

    puts(_failures ? "FAIL" : "PASS");
    return _failures ? 1 : 0;
}
