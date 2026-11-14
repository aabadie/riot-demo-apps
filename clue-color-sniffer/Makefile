APPLICATION = clue-color-sniffer
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += apds9960
USEMODULE += ztimer_msec

USEPKG += emlearn

CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
