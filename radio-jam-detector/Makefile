APPLICATION = radio-jam-detector
BOARD ?= iotlab-m3

include ../Makefile.common

# the radio is used through the IEEE 802.15.4 Radio HAL, without network
# stack: AT86RF2xx (iotlab-m3, samr21-xpro) or the nRF52840 radio
BOARD_WHITELIST := iotlab-m3 samr21-xpro nrf52840dk

USEMODULE += netdev_default
USEMODULE += ieee802154
# the AT86RF2xx events run in an event thread
USEMODULE += event_thread
# basic mode of the AT86RF2xx: frames sent without CSMA-CA nor retries,
# all frames received and the bad CRCs reported
CFLAGS += -DAT86RF2XX_BASIC_MODE=1
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += emlearn

include $(RIOTBASE)/Makefile.include
