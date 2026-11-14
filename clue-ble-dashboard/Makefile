APPLICATION = clue-ble-dashboard
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += apds9960
USEMODULE += demo_accel
USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += saul_default
USEMODULE += ztimer_msec

USEPKG += nimble
USEMODULE += nimble_svc_gap
USEMODULE += nimble_svc_gatt
USEMODULE += nimble_autoadv
CFLAGS += -DCONFIG_NIMBLE_AUTOADV_DEVICE_NAME='"CLUE Dashboard"'
CFLAGS += -DCONFIG_NIMBLE_AUTOADV_START_MANUALLY=1

# the color recognition of Color Sniffer
USEPKG += emlearn
INCLUDES += -I$(DEMO_APPS_BASE)/clue-color-sniffer

CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
