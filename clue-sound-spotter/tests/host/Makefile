# Host test of the training pipeline on synthetic sounds: sound.c
# computes the features, the exported model is built with sound_eml.c
# and must agree with scikit-learn. The model of the application is left
# untouched.
PYTHON ?= $(abspath ../../../.venv/bin/python)
OUT := $(CURDIR)/out

test:
	mkdir -p $(OUT)
	$(PYTHON) ../../tools/train.py --synthetic --output $(OUT)/model.h --min-accuracy 0.95

clean:
	rm -rf $(OUT)

.PHONY: test clean
