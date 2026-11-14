# Host test of the training: the game of pong.c through ctypes, the
# network imitates the scripted player, then plays in C with model.h. The
# model of the application is left untouched.
PYTHON ?= $(abspath ../../../.venv/bin/python)
OUT := $(CURDIR)/out

test:
	mkdir -p $(OUT)
	$(PYTHON) ../../tools/train.py --output $(OUT)/model.h --min-returns 3

clean:
	rm -rf $(OUT)

.PHONY: test clean
