# Host test of the zones, needs the emlearn headers
EMLEARN ?= ../../../../RIOT-drivers/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: test_zones
	./test_zones

test_zones: test_zones.c ../../zones.c ../../zones.h
	$(CC) $(CFLAGS) -o $@ test_zones.c ../../zones.c -lm

clean:
	rm -f test_zones

.PHONY: test clean
