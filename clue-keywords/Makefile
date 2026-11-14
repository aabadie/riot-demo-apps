APPLICATION = clue-keywords
BOARD ?= adafruit-clue

include ../Makefile.common

FEATURES_REQUIRED += periph_pdm

USEMODULE += demo_board
USEMODULE += demo_tone
USEMODULE += core_thread_flags
USEMODULE += ztimer_msec

USEPKG += emlearn

USEPKG += lvgl
USEMODULE += lvgl_contrib
USEMODULE += lvgl_widget_bar
USEMODULE += lvgl_widget_label
USEMODULE += lvgl_theme_default
USEMODULE += lvgl_theme_default_dark

CFLAGS += -DLV_FONT_MONTSERRAT_14=1
CFLAGS += -DLV_FONT_MONTSERRAT_28=1
CFLAGS += '-DLV_MEM_SIZE=(32U*1024U)'
CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
