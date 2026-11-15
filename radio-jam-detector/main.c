/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Jam Detector: interference detection on the IEEE 802.15.4
 *              channels, with a Gaussian mixture learned on the node
 *
 * The node scans the 16 channels twice a second: on each channel, clear
 * channel assessments at three energy thresholds tell how busy it is. For
 * a minute it learns what is usual, then it flags the scans that are not,
 * and tells which channels are hit. See detector.c.
 *
 * The same firmware can be a test transmitter, to make some interference
 * on purpose: see the `interfere` command.
 *
 * Serial output, one line per scan:
 *
 *     S <scan> <state> <level> <occupancy> <frames> <crc errors>
 *
 * state is L (learning), D (detecting) or A (alarm), level is ten times the
 * detector level, occupancy has one hex digit per channel from 11 to 26 (0:
 * always clear, f: always busy).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irq.h"
#include "shell.h"
#include "thread.h"
#include "ztimer.h"

#include "detector.h"
#include "radio.h"

#define SCAN_MS         (500U)      /* one scan every half second */
#define PASSES          (8U)        /* visits of each channel per scan */
#define LEARN_S         (60U)
#define ALARM_SCANS     (3U)        /* unusual scans in a row to raise the alarm */
#define CLEAR_SCANS     (5U)        /* usual scans in a row to clear it */
#define INTERFERE_MAX_S (60U)
#define SCAN_TX_POWER   (0)

static const int8_t _thresholds[] = { -85, -75, -65 };

#define SAMPLES         (PASSES * ARRAY_SIZE(_thresholds))

typedef enum {
    MODE_SCAN,
    MODE_INTERFERE,
} scan_mode_t;

/* settings from the shell, read by the radio thread */
static volatile scan_mode_t _mode = MODE_SCAN;
static volatile unsigned _learn_scans = LEARN_S * 1000 / SCAN_MS;
static volatile bool _relearn = true;
static volatile uint8_t _jam_channel;
static volatile unsigned _jam_seconds;
static volatile unsigned _jam_duty;
static volatile int8_t _jam_power;
/* a new interfere command, its settings replace the ones of the burst */
static volatile bool _jam_new;

static char _radio_stack[THREAD_STACKSIZE_DEFAULT + 1024];

static void _scan(float occupancy[DET_CHANNELS])
{
    unsigned busy[DET_CHANNELS] = { 0 };

    /* several passes over the channels, so that each channel is sampled
       across the whole scan */
    for (unsigned p = 0; p < PASSES; p++) {
        for (unsigned c = 0; c < DET_CHANNELS; c++) {
            if (radio_set_channel(DET_FIRST_CHANNEL + c, SCAN_TX_POWER) < 0) {
                continue;
            }
            for (unsigned t = 0; t < ARRAY_SIZE(_thresholds); t++) {
                busy[c] += radio_busy(_thresholds[t]) == 1;
            }
        }
    }
    for (unsigned c = 0; c < DET_CHANNELS; c++) {
        occupancy[c] = (float)busy[c] / SAMPLES;
    }
}

static void _interfere(void)
{
    unsigned state = irq_disable();
    uint8_t channel = _jam_channel;
    unsigned duty = _jam_duty;
    unsigned seconds = _jam_seconds;
    int8_t power = _jam_power;
    _jam_new = false;
    irq_restore(state);

    uint32_t air = radio_test_frame_us();
    uint32_t gap = air * (100 - duty) / duty;
    uint32_t end = ztimer_now(ZTIMER_MSEC) + seconds * 1000;
    unsigned sent = 0, errors = 0;

    printf("I start channel %u duty %u%% power %d dBm for %u s\n", channel, duty,
           power, seconds);
    radio_set_channel(channel, power);
    while (_mode == MODE_INTERFERE && !_jam_new &&
           (int32_t)(end - ztimer_now(ZTIMER_MSEC)) > 0) {
        if (radio_send_test_frame() == 0) {
            sent++;
        }
        else {
            errors++;
        }
        if (gap) {
            ztimer_sleep(ZTIMER_USEC, gap);
        }
    }
    printf("I done %u frames sent, %u errors\n", sent, errors);
    /* back to scanning, unless a new burst was asked for meanwhile */
    state = irq_disable();
    if (!_jam_new && _mode == MODE_INTERFERE) {
        _mode = MODE_SCAN;
    }
    irq_restore(state);
}

static void *_radio_thread(void *arg)
{
    (void)arg;
    float occupancy[DET_CHANNELS];
    det_result_t result;
    radio_rx_count_t rx;
    unsigned scan = 0, learned = 0, unusual = 0, usual = 0;
    bool alarm = false;
    uint32_t last = ztimer_now(ZTIMER_MSEC);

    while (1) {
        if (_mode == MODE_INTERFERE) {
            _interfere();
            last = ztimer_now(ZTIMER_MSEC);
            continue;
        }
        if (_relearn) {
            _relearn = false;
            det_learn_reset();
            learned = 0;
            alarm = false;
            unusual = usual = 0;
            printf("L learning for %u scans\n", _learn_scans);
        }

        _scan(occupancy);
        radio_rx_count(&rx);
        if (_relearn || _mode == MODE_INTERFERE) {
            /* a command came during the scan, it applies from the next one */
            continue;
        }
        scan++;

        char state = 'L';
        memset(&result, 0, sizeof(result));
        if (!det_trained()) {
            learned = det_learn_add(occupancy);
            if (learned >= _learn_scans || learned >= DET_TRAIN_MAX) {
                if (det_learn_fit() == 0) {
                    printf("L learned, usual occupancy");
                    for (unsigned c = 0; c < DET_CHANNELS; c++) {
                        printf(" %u", (unsigned)(det_learned_mean(c) * 100 + 0.5f));
                    }
                    puts(" %");
                }
            }
        }
        else {
            det_score(occupancy, &result);
            bool odd = result.level >= DET_ALARM_LEVEL;
            unusual = odd ? unusual + 1 : 0;
            usual = odd ? 0 : usual + 1;
            if (!alarm && unusual >= ALARM_SCANS) {
                alarm = true;
                if (result.kind == DET_UNUSUAL) {
                    printf("ALARM %s\n", det_kind_name(result.kind));
                }
                else {
                    printf("ALARM %s channels %u-%u\n", det_kind_name(result.kind),
                           result.first, result.last);
                }
            }
            else if (alarm && usual >= CLEAR_SCANS) {
                alarm = false;
                puts("CLEAR");
            }
            state = alarm ? 'A' : 'D';
        }

        char hex[DET_CHANNELS + 1];
        for (unsigned c = 0; c < DET_CHANNELS; c++) {
            hex[c] = "0123456789abcdef"[(unsigned)(occupancy[c] * 15 + 0.5f)];
        }
        hex[DET_CHANNELS] = '\0';
        printf("S %u %c %u %s %u %u\n", scan, state, (unsigned)(result.level * 10 + 0.5f),
               hex, rx.frames, rx.crc_errors);

        uint32_t now = ztimer_now(ZTIMER_MSEC);
        if (now - last < SCAN_MS) {
            ztimer_sleep(ZTIMER_MSEC, SCAN_MS - (now - last));
        }
        last = ztimer_now(ZTIMER_MSEC);
    }
    return NULL;
}

static int _cmd_learn(int argc, char **argv)
{
    unsigned seconds = argc > 1 ? (unsigned)atoi(argv[1]) : LEARN_S;
    unsigned scans = seconds * 1000 / SCAN_MS;

    if (scans < 4 * DET_COMPONENTS || scans > DET_TRAIN_MAX) {
        printf("usage: %s [seconds, %u to %u]\n", argv[0],
               4 * DET_COMPONENTS * SCAN_MS / 1000 + 1, DET_TRAIN_MAX * SCAN_MS / 1000);
        return 1;
    }
    _learn_scans = scans;
    _mode = MODE_SCAN;
    _relearn = true;
    return 0;
}

static int _cmd_interfere(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <channel> [seconds=10] [duty %%=50] [power dBm=-17]\n", argv[0]);
        return 1;
    }
    int channel = atoi(argv[1]);
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    int duty = argc > 3 ? atoi(argv[3]) : 50;
    int power = argc > 4 ? atoi(argv[4]) : -17;

    if (channel < (int)DET_FIRST_CHANNEL || channel >= (int)(DET_FIRST_CHANNEL + DET_CHANNELS) ||
        seconds < 1 || seconds > (int)INTERFERE_MAX_S || duty < 1 || duty > 100 ||
        power < -40 || power > 4) {
        printf("channel 11 to 26, 1 to %u s, duty 1 to 100 %%, power -40 to 4 dBm\n",
               INTERFERE_MAX_S);
        return 1;
    }
    unsigned state = irq_disable();
    _jam_channel = (uint8_t)channel;
    _jam_seconds = (unsigned)seconds;
    _jam_duty = (unsigned)duty;
    _jam_power = (int8_t)power;
    _jam_new = true;
    _mode = MODE_INTERFERE;
    irq_restore(state);
    return 0;
}

static int _cmd_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    _mode = MODE_SCAN;
    return 0;
}

SHELL_COMMAND(learn, "learn the usual occupancy again [seconds]", _cmd_learn);
SHELL_COMMAND(interfere, "send test frames <channel> [s] [duty %] [dBm]", _cmd_interfere);
SHELL_COMMAND(scan, "stop sending and scan again", _cmd_scan);

int main(void)
{
    int res = radio_init();

    puts("Jam Detector");
    if (res < 0) {
        printf("radio init failed: %d\n", res);
        return 1;
    }
    thread_create(_radio_stack, sizeof(_radio_stack), THREAD_PRIORITY_MAIN - 1, 0,
                  _radio_thread, NULL, "radio");

    char line[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line, sizeof(line));
    return 0;
}
