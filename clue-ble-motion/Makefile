APPLICATION = clue-ble-motion
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_accel
USEMODULE += demo_board
USEMODULE += demo_gfx
# the magnetometer through SAUL
USEMODULE += saul_default
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec
USEMODULE += ztimer_periodic

USEPKG += nimble
USEMODULE += nimble_svc_gap
USEMODULE += nimble_svc_gatt
USEMODULE += nimble_autoadv
CFLAGS += -DCONFIG_NIMBLE_AUTOADV_DEVICE_NAME='"CLUE Motion"'
CFLAGS += -DCONFIG_NIMBLE_AUTOADV_START_MANUALLY=1

include $(RIOTBASE)/Makefile.include
