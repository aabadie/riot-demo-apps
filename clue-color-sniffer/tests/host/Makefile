# Host test of the color recognition, needs the emlearn headers
EMLEARN ?= ../../../RIOT/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: test_colors
	./test_colors

test_colors: test_colors.c ../../colors.c ../../colors.h
	$(CC) $(CFLAGS) -o $@ test_colors.c ../../colors.c -lm

clean:
	rm -f test_colors

.PHONY: test clean
