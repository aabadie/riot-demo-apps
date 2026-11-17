# Host test of the training pipeline on synthetic sounds: sound.c
# computes the features, the exported model is built with sound_eml.c
# and must agree with scikit-learn. The model of the application is left
# untouched.
PYTHON ?= $(abspath ../../../.venv/bin/python)
OUT := $(CURDIR)/out
EMLEARN ?= ../../../RIOT/build/pkg/emlearn/emlearn
CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: $(OUT)/test_detect
	$(OUT)/test_detect
	mkdir -p $(OUT)
	$(PYTHON) ../../tools/train.py --synthetic --output $(OUT)/model.h --min-accuracy 0.95

$(OUT)/test_detect: test_detect.c ../../sound.c ../../sound_eml.c ../../sound.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ test_detect.c ../../sound.c ../../sound_eml.c -lm

clean:
	rm -rf $(OUT)

.PHONY: test clean
