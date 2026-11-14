APPLICATION = radio-tinyfl
BOARD ?= iotlab-m3

include ../Makefile.common

ifneq (,$(filter native native32 native64,$(BOARD)))
  # simulated IEEE 802.15.4 radio, the nodes meet through the ZEP
  # dispatcher of RIOT (dist/tools/zep_dispatch)
  USE_ZEP := 1
  USEMODULE += socket_zep
  USEMODULE += netdev
else
  USEMODULE += netdev_default
  # printf and the sock in the main thread
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=3072
endif

USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_netif_single
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_sock_udp
USEMODULE += random
USEMODULE += ztimer_msec

USEPKG += emlearn

include $(RIOTBASE)/Makefile.include
include $(RIOTMAKE)/default-radio-settings.inc.mk
