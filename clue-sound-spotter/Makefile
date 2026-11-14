APPLICATION = clue-sound-spotter
BOARD ?= adafruit-clue

include ../Makefile.common

FEATURES_REQUIRED += periph_pdm

USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += core_thread_flags
USEMODULE += ztimer_msec

USEPKG += emlearn

CFLAGS += -DSOUND_HAVE_MODEL
CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
