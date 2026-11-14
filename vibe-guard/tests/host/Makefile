# Host test of the vibration anomaly detection, needs the emlearn headers
EMLEARN ?= ../../../../RIOT-drivers/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: test_vibe
	./test_vibe

test_vibe: test_vibe.c ../../vibe.c ../../vibe.h
	$(CC) $(CFLAGS) -o $@ test_vibe.c ../../vibe.c -lm

clean:
	rm -f test_vibe

.PHONY: test clean
