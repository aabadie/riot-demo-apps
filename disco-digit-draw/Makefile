APPLICATION = disco-digit-draw
BOARD ?= stm32f723e-disco

include ../Makefile.common

USEMODULE += demo_gfx
USEMODULE += periph_gpio
USEMODULE += touch_dev
USEMODULE += ztimer_msec

USEPKG += emlearn

# the model and the digit preparation of digit-oracle
CFLAGS += -I$(DEMO_APPS_BASE)/digit-oracle
CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
