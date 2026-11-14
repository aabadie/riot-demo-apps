APPLICATION = spell-caster
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_accel
USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += random
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += emlearn

ifeq (,$(filter native%,$(BOARD)))
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096
endif

include $(RIOTBASE)/Makefile.include
