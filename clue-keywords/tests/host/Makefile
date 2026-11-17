# Host test of the keyword spotter, needs the emlearn headers
EMLEARN ?= ../../../RIOT/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I../.. -I$(EMLEARN)

test: test_kws
	./test_kws

test_kws: test_kws.c ../../kws.c ../../kws.h
	$(CC) $(CFLAGS) -o $@ test_kws.c ../../kws.c -lm

clean:
	rm -f test_kws

.PHONY: test clean
