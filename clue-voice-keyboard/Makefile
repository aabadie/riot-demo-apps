APPLICATION = clue-voice-keyboard
BOARD ?= adafruit-clue

include ../Makefile.common

# Layout of the computer the keyboard types on: qwerty or azerty
KEYMAP ?= qwerty

FEATURES_REQUIRED += periph_pdm
FEATURES_REQUIRED += periph_flashpage
FEATURES_REQUIRED += periph_flashpage_in_address_space

USEMODULE += demo_board
USEMODULE += demo_gfx
USEMODULE += demo_tone
USEMODULE += core_thread_flags
USEMODULE += ztimer_msec

# the keyboard and the serial console share the USB device, which the
# application initializes itself
USEMODULE += usbus_hid
DISABLE_MODULE += auto_init_usbus

# a USB device with its own functions needs its own IDs, these are the
# testing IDs of RIOT
USB_VID ?= $(USB_VID_TESTING)
USB_PID ?= $(USB_PID_TESTING)

USEPKG += emlearn

ifeq (azerty,$(KEYMAP))
  CFLAGS += -DKEYS_AZERTY
else ifneq (qwerty,$(KEYMAP))
  $(error KEYMAP must be qwerty or azerty)
endif

CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
