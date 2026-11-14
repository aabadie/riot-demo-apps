# Host test of the federated naive Bayes, needs the emlearn headers
EMLEARN ?= ../../../../RIOT-drivers/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -I../.. -I$(EMLEARN)

test: test_fl
	./test_fl

test_fl: test_fl.c ../../fl.c ../../fl.h
	$(CC) $(CFLAGS) -o $@ test_fl.c ../../fl.c -lm

clean:
	rm -f test_fl

.PHONY: test clean
