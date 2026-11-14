# Host build of the digit classifier, needs the emlearn headers
EMLEARN ?= ../../../../RIOT-drivers/build/pkg/emlearn/emlearn
PYTHON ?= $(abspath ../../../.venv/bin/python)

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: digit_host
	$(PYTHON) ../../tools/send_digits.py --exec ./digit_host --count 10000 --quiet --min-accuracy 0.97
	$(PYTHON) ../../tools/send_digits.py --exec ./digit_host --count 2000 --quiet --normalize --min-accuracy 0.90

digit_host: digit_host.c ../../digits.c ../../protocol.c ../../digits.h ../../protocol.h ../../model.h
	$(CC) $(CFLAGS) -o $@ digit_host.c ../../digits.c ../../protocol.c -lm

clean:
	rm -f digit_host

.PHONY: test clean

# The whole application built for native64, through its stdin and stdout
RIOTBASE ?= $(abspath ../../../../RIOT-drivers)
NATIVE_ELF = ../../bin/native64/digit-oracle.elf

native:
	$(MAKE) -C ../.. BOARD=native64 RIOTBASE=$(RIOTBASE)
	$(PYTHON) ../../tools/send_digits.py --exec $(NATIVE_ELF) --count 1000 --quiet --min-accuracy 0.97

.PHONY: native
