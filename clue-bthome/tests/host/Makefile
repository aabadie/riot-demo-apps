# Host test of the BTHome encoder, decoded with bthome-ble from the venv of
# the repository (pip install bthome-ble)
PYTHON ?= $(abspath ../../../.venv/bin/python)

CFLAGS += -O2 -Wall -Wextra -Werror -I../..

test: test_bthome
	./test_bthome | $(PYTHON) decode.py

test_bthome: test_bthome.c ../../bthome.c ../../bthome.h
	$(CC) $(CFLAGS) -o $@ test_bthome.c ../../bthome.c -lm

clean:
	rm -f test_bthome

.PHONY: test clean
