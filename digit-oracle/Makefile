APPLICATION = digit-oracle
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_accel
USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += stdin
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += emlearn

# A whole image is sent on one line of 1572 characters
CFLAGS += -DSTDIO_RX_BUFSIZE=2048
# native, used for tests/host/native, has larger default stacks
ifeq (,$(filter native%,$(BOARD)))
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096
endif

include $(RIOTBASE)/Makefile.include
