APPLICATION = clue-bthome
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_accel
USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += saul_default
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += nimble
USEMODULE += bluetil_ad

# the vibration anomaly detection of Vibe Guard
USEPKG += emlearn
INCLUDES += -I$(DEMO_APPS_BASE)/vibe-guard

CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
