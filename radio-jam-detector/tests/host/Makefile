# Host test of the interference detector, needs the emlearn headers
EMLEARN ?= ../../../../RIOT-drivers/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: test_detector
	./test_detector

test_detector: test_detector.c ../../detector.c ../../detector.h
	$(CC) $(CFLAGS) -o $@ test_detector.c ../../detector.c -lm

clean:
	rm -f test_detector

.PHONY: test clean
