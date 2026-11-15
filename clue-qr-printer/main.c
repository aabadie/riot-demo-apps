/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       QR Printer: prints the QR code of a text on the thermal
 *              printer of Adafruit
 *
 * The text comes from the `qr` shell command, or from a phone over BLE.
 * The board shows the code on its screen while the printer prints it,
 * button A prints the last one again.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "adafruit_thermal.h"
#include "adafruit_thermal_params.h"
#include "mutex.h"
#include "shell.h"
#include "thread.h"
#include "ztimer.h"

#if IS_USED(MODULE_DEMO_BOARD)
#include "demo_board.h"
#endif

#include "app.h"
#include "qr_print.h"

#define POLL_MS         (50U)
/* the shell line: the command, the quotes and the text */
#define LINE_LEN        (APP_TEXT_MAX + 16)

static adafruit_thermal_t _printer;
static bool _printer_ok;
/* below the shell: the UART driver may wait actively for each row */
static char _stack[THREAD_STACKSIZE_DEFAULT + 1024];

/* written by the shell and the BLE threads, read by the printer thread */
static mutex_t _lock = MUTEX_INIT;
static char _queued[APP_TEXT_MAX + 1];
static bool _pending;
static bool _busy;

/* printer thread only */
static char _text[APP_TEXT_MAX + 1];
static uint8_t _qr[QR_BUFFER_LEN];
static bool _have_last;

int app_submit(const char *text, size_t len)
{
    if ((len == 0) || memchr(text, '\0', len)) {
        return -EINVAL;
    }
    if (len > APP_TEXT_MAX) {
        return -EMSGSIZE;
    }
    mutex_lock(&_lock);
    if (_busy) {
        mutex_unlock(&_lock);
        return -EBUSY;
    }
    memcpy(_queued, text, len);
    _queued[len] = '\0';
    _pending = true;
    _busy = true;
    mutex_unlock(&_lock);
    return 0;
}

static void _state(uint8_t state)
{
#if IS_USED(MODULE_NIMBLE_HOST)
    app_ble_state(state);
#else
    (void)state;
#endif
}

static void _show(const uint8_t *qr, const char *status)
{
    printf("%s\n", status);
#if IS_USED(MODULE_DEMO_GFX)
    app_screen_show(qr, status);
#else
    (void)qr;
#endif
}

static void _print(void)
{
    char status[32];

    if (!qr_encode(_text, _qr)) {
        /* cannot happen with APP_TEXT_MAX bytes */
        _show(NULL, "Text too long");
        return;
    }
    _have_last = true;
    snprintf(status, sizeof(status), "Printing %u bytes", (unsigned)strlen(_text));
    _show(_qr, status);
    if (_printer_ok) {
        qr_print(&_printer, _qr);
        qr_print_caption(&_printer, _text);
        adafruit_thermal_feed(&_printer, 3);
    }
    snprintf(status, sizeof(status), "Printed, version %u",
             (unsigned)((qrcodegen_getSize(_qr) - 17) / 4));
    _show(_qr, status);
}

static bool _reprint_pressed(void)
{
#if IS_USED(MODULE_DEMO_BOARD)
    static buttons_t buttons;
    buttons_update(&buttons);
    return buttons.pressed & BTN_A;
#else
    return false;
#endif
}

static void *_printer_thread(void *arg)
{
    (void)arg;

    while (1) {
        bool reprint = _reprint_pressed();
        bool run = false;

        mutex_lock(&_lock);
        if (_pending) {
            memcpy(_text, _queued, sizeof(_text));
            _pending = false;
            run = true;
        }
        else if (reprint && _have_last && !_busy) {
            _busy = true;
            run = true;
        }
        mutex_unlock(&_lock);

        if (run) {
            _state(APP_STATE_PRINTING);
            _print();
            mutex_lock(&_lock);
            _busy = false;
            mutex_unlock(&_lock);
            _state(APP_STATE_READY);
        }
        ztimer_sleep(ZTIMER_MSEC, POLL_MS);
    }
    return NULL;
}

static int _cmd_qr(int argc, char **argv)
{
    static char text[APP_TEXT_MAX + 1];
    size_t len = 0;

    if (argc < 2) {
        printf("usage: %s <text>, quote it to keep several spaces\n", argv[0]);
        return 1;
    }
    /* the shell splits the line at the spaces, join the words again */
    for (int i = 1; i < argc; i++) {
        size_t word = strlen(argv[i]);
        if (len + word + (i > 1) > APP_TEXT_MAX) {
            printf("text too long: at most %u bytes\n", APP_TEXT_MAX);
            return 1;
        }
        if (i > 1) {
            text[len++] = ' ';
        }
        memcpy(&text[len], argv[i], word);
        len += word;
    }

    int res = app_submit(text, len);
    if (res == -EBUSY) {
        puts("busy: the printer prints another text");
        return 1;
    }
    if (res < 0) {
        printf("cannot print: %d\n", res);
        return 1;
    }
    printf("queued %u bytes\n", (unsigned)len);
    return 0;
}

static const shell_command_t _commands[] = {
    { "qr", "print the QR code of a text", _cmd_qr },
    { NULL, NULL, NULL }
};

int main(void)
{
    static char line[LINE_LEN];

    puts("QR Printer");

#if IS_USED(MODULE_DEMO_BOARD)
    buttons_init();
#endif
#if IS_USED(MODULE_DEMO_GFX)
    app_screen_init();
#endif
    _printer_ok = adafruit_thermal_init(&_printer, &adafruit_thermal_params[0]) == 0;
    if (!_printer_ok) {
        puts("no printer UART: the codes are only shown");
    }
#if IS_USED(MODULE_NIMBLE_HOST)
    app_ble_init();
#endif
    _show(NULL, "Ready");

    thread_create(_stack, sizeof(_stack), THREAD_PRIORITY_MAIN + 1, 0,
                  _printer_thread, NULL, "printer");

    shell_run(_commands, line, sizeof(line));
    return 0;
}
