/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Channel scan and test transmitter on the IEEE 802.15.4 Radio
 *              HAL of RIOT
 *
 * Everything runs in the thread that calls these functions. The radio
 * callback only records events: received frames are dropped by the next
 * call, which keeps the radio out of the event thread.
 */

#include <errno.h>
#include <string.h>

#include "byteorder.h"
#include "event/thread.h"
#include "iolist.h"
#include "mutex.h"
#include "net/ieee802154.h"
#include "net/ieee802154/radio.h"

#ifdef MODULE_AT86RF2XX
#include "at86rf2xx.h"
#include "at86rf2xx_params.h"
#endif
#ifdef MODULE_NRF802154
#include "nrf802154.h"
#endif

#include "radio.h"

#define TEST_PANID          (0xbeefU)
#define TEST_PAYLOAD_LEN    (100U)

static ieee802154_dev_t _radio;
static mutex_t _done = MUTEX_INIT_LOCKED;
/* the AT86RF2xx also reports the CCA polled by ieee802154_radio_cca(), only
   a CCA that is waited for unlocks _done */
static volatile bool _wait_cca;
static volatile unsigned _rx_pending;
static volatile unsigned _frames;
static volatile unsigned _crc_errors;
static uint8_t _seq;

#ifdef MODULE_AT86RF2XX
static at86rf2xx_bhp_ev_t _bhp;
#endif

static void _radio_cb(ieee802154_dev_t *dev, ieee802154_trx_ev_t event)
{
    (void)dev;
    switch (event) {
    case IEEE802154_RADIO_CONFIRM_TX_DONE:
        mutex_unlock(&_done);
        break;
    case IEEE802154_RADIO_CONFIRM_CCA:
        if (_wait_cca) {
            mutex_unlock(&_done);
        }
        break;
    case IEEE802154_RADIO_INDICATION_RX_DONE:
        _frames++;
        _rx_pending = 1;
        break;
    case IEEE802154_RADIO_INDICATION_CRC_ERROR:
        _crc_errors++;
        _rx_pending = 1;
        break;
    default:
        break;
    }
}

/* drop a received frame, the radio keeps it until it is read */
static void _drop_rx(void)
{
    if (_rx_pending) {
        _rx_pending = 0;
        ieee802154_radio_set_idle(&_radio, true);
        ieee802154_radio_read(&_radio, NULL, 0, NULL);
        ieee802154_radio_set_rx(&_radio);
    }
}

int radio_init(void)
{
    int res;

#if defined(MODULE_AT86RF2XX)
    res = at86rf2xx_init_event(&_bhp, &at86rf2xx_params[0], &_radio, EVENT_PRIO_HIGHEST);
    if (res < 0) {
        return res;
    }
#elif defined(MODULE_NRF802154)
    nrf802154_hal_setup(&_radio);
    res = nrf802154_init();
    if (res < 0) {
        return res;
    }
#else
#error "radio-jam-detector: no supported IEEE 802.15.4 radio"
#endif
    _radio.cb = _radio_cb;

    res = ieee802154_radio_request_on(&_radio);
    if (res < 0) {
        return res;
    }
    while ((res = ieee802154_radio_confirm_on(&_radio)) == -EAGAIN) {}
    if (res < 0) {
        return res;
    }
    ieee802154_radio_set_frame_filter_mode(&_radio, IEEE802154_FILTER_ACCEPT);
    /* the test frames are sent without listening first */
    res = ieee802154_radio_set_csma_params(&_radio, NULL, -1);
    if (res < 0) {
        return res;
    }
    res = ieee802154_radio_set_cca_mode(&_radio, IEEE802154_CCA_MODE_ED_THRESHOLD);
    if (res < 0) {
        return res;
    }
    return radio_set_channel(IEEE802154_CHANNEL_MIN, 0);
}

int radio_set_channel(uint8_t channel, int8_t tx_power)
{
    ieee802154_phy_conf_t conf = {
        .phy_mode = IEEE802154_PHY_OQPSK, .channel = channel, .pow = tx_power,
    };

    ieee802154_radio_set_idle(&_radio, true);
    if (_rx_pending) {
        _rx_pending = 0;
        ieee802154_radio_read(&_radio, NULL, 0, NULL);
    }
    int res = ieee802154_radio_config_phy(&_radio, &conf);
    ieee802154_radio_set_rx(&_radio);
    return res;
}

int radio_busy(int8_t threshold_dbm)
{
    int res;

    _drop_rx();
    res = ieee802154_radio_set_cca_threshold(&_radio, threshold_dbm);
    if (res < 0) {
        return res;
    }
    if (ieee802154_radio_has_irq_cca_done(&_radio)) {
        _wait_cca = true;
        res = ieee802154_radio_request_cca(&_radio);
        if (res < 0) {
            _wait_cca = false;
            return res;
        }
        mutex_lock(&_done);
        _wait_cca = false;
        res = ieee802154_radio_confirm_cca(&_radio);
        /* the nRF52840 radio stays idle after a CCA, receive again */
        ieee802154_radio_set_rx(&_radio);
    }
    else {
        res = ieee802154_radio_cca(&_radio);
    }
    if (res < 0) {
        return res;
    }
    /* positive when the channel is clear */
    return res > 0 ? 0 : 1;
}

void radio_rx_count(radio_rx_count_t *count)
{
    _drop_rx();
    count->frames = _frames;
    count->crc_errors = _crc_errors;
    _frames = 0;
    _crc_errors = 0;
}

int radio_send_test_frame(void)
{
    static uint8_t payload[TEST_PAYLOAD_LEN];
    uint8_t mhr[IEEE802154_MAX_HDR_LEN];
    const uint8_t src[2] = { 0xfe, 0xca };
    const uint8_t dst[2] = { 0xff, 0xff };
    le_uint16_t pan = byteorder_htols(TEST_PANID);
    ieee802154_tx_info_t info = { .status = TX_STATUS_SUCCESS };
    int res;

    memset(payload, 0x55, sizeof(payload));
    int len = ieee802154_set_frame_hdr(mhr, src, sizeof(src), dst, sizeof(dst), pan, pan,
                                       IEEE802154_FCF_TYPE_DATA, _seq++);
    if (len <= 0) {
        return -EINVAL;
    }
    iolist_t data = { .iol_base = payload, .iol_len = sizeof(payload) };
    iolist_t hdr = { .iol_next = &data, .iol_base = mhr, .iol_len = (size_t)len };

    _drop_rx();
    res = ieee802154_radio_request_set_idle(&_radio, true);
    if (res < 0) {
        return res;
    }
    ieee802154_radio_write(&_radio, &hdr);
    while ((res = ieee802154_radio_confirm_set_idle(&_radio)) == -EAGAIN) {}
    if (res < 0) {
        return res;
    }
    /* forget an unlock left by an event that was not waited for */
    mutex_trylock(&_done);
    while ((res = ieee802154_radio_request_transmit(&_radio)) == -EBUSY) {}
    if (res < 0) {
        ieee802154_radio_set_rx(&_radio);
        return res;
    }
    mutex_lock(&_done);
    while ((res = ieee802154_radio_confirm_transmit(&_radio, &info)) == -EAGAIN) {}
    ieee802154_radio_set_rx(&_radio);
    if (res < 0) {
        return res;
    }
    return info.status == TX_STATUS_MEDIUM_BUSY ? -EBUSY : 0;
}

uint32_t radio_test_frame_us(void)
{
    /* preamble, SFD and length, header of 9 bytes, payload and FCS, 32 us
       per byte at 250 kbit/s */
    return (4 + 1 + 1 + 9 + TEST_PAYLOAD_LEN + 2) * 32;
}
