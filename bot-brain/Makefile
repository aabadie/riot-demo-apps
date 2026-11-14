APPLICATION = bot-brain
BOARD ?= adafruit-pybadge

include ../Makefile.common

USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += random
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += emlearn

CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
