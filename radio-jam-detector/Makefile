APPLICATION = radio-jam-detector
BOARD ?= iotlab-m3

include ../Makefile.common

# the radio is used through the IEEE 802.15.4 Radio HAL, without network
# stack: AT86RF2xx (iotlab-m3, samr21-xpro) or the nRF52840 radio
BOARD_WHITELIST := iotlab-m3 samr21-xpro nrf52840dk

USEMODULE += netdev_default
USEMODULE += ieee802154
USEMODULE += event_thread
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

USEPKG += emlearn

# the radio events run in the event thread
CFLAGS += -DEVENT_THREAD_MEDIUM_STACKSIZE=1024

include $(RIOTBASE)/Makefile.include

ifneq (,$(filter bhp,$(USEMODULE)))
  USEMODULE += bhp_event
endif
