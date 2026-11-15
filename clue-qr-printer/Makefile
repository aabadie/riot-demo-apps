APPLICATION = clue-qr-printer
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += adafruit_thermal
USEMODULE += shell
USEMODULE += ztimer_msec
USEPKG += qr-code-generator

# the printer on the UART of the edge connector, pad 1 (TX, P0.05)
PRINTER_UART ?= 0
PRINTER_BAUDRATE ?= 19200
CFLAGS += -DADAFRUIT_THERMAL_PARAM_UART='UART_DEV($(PRINTER_UART))'
CFLAGS += -DADAFRUIT_THERMAL_PARAM_BAUDRATE=$(PRINTER_BAUDRATE)
# the DTR pin of the printer, e.g. PRINTER_DTR='GPIO_PIN(0,3)' for pad 2
ifneq (,$(PRINTER_DTR))
  CFLAGS += -DADAFRUIT_THERMAL_PARAM_DTR_PIN='$(PRINTER_DTR)'
endif

# a pasted text of APP_TEXT_MAX bytes, read while the shell echoes it
CFLAGS += -DSTDIO_RX_BUFSIZE=1024

ifneq (,$(filter native native64,$(BOARD)))
  # the shell only, the printer on a host serial port: see tests/native
  SRC = main.c qr_print.c
else
  USEMODULE += demo_board
  USEMODULE += demo_gfx

  USEPKG += nimble
  USEMODULE += nimble_svc_gap
  USEMODULE += nimble_svc_gatt
  USEMODULE += nimble_autoadv
  CFLAGS += -DCONFIG_NIMBLE_AUTOADV_DEVICE_NAME='"CLUE QR Printer"'
  CFLAGS += -DCONFIG_NIMBLE_AUTOADV_START_MANUALLY=1
endif

include $(RIOTBASE)/Makefile.include
