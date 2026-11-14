APPLICATION = ble-zones
BOARD ?= dwm1001

include ../Makefile.common

BOARD_WHITELIST := adafruit-clue dwm1001 nrf52dk nrf52840dk

USEMODULE += shell
USEMODULE += ztimer_msec

USEPKG += nimble
USEMODULE += nimble_scanner

USEPKG += emlearn

include $(RIOTBASE)/Makefile.include
