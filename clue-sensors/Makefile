APPLICATION = clue-sensors
BOARD ?= adafruit-clue

include ../Makefile.common

USEMODULE += demo_board
USEMODULE += saul_default
USEMODULE += ztimer_msec

USEPKG += lvgl
USEMODULE += lvgl_contrib
USEMODULE += lvgl_widget_arc
USEMODULE += lvgl_widget_bar
USEMODULE += lvgl_widget_chart
USEMODULE += lvgl_widget_label
USEMODULE += lvgl_widget_line
USEMODULE += lvgl_theme_default
USEMODULE += lvgl_theme_default_dark

CFLAGS += -DLV_FONT_MONTSERRAT_14=1
CFLAGS += -DLV_FONT_MONTSERRAT_28=1
CFLAGS += '-DLV_MEM_SIZE=(32U*1024U)'
CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

include $(RIOTBASE)/Makefile.include
