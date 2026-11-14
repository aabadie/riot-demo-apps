# Host test of the packets of the dashboard, decoded with the protocol.js of
# the page (needs node)
CFLAGS += -O2 -Wall -Wextra -Werror -I../..

test: test_protocol
	./test_protocol | node decode.mjs

test_protocol: test_protocol.c ../../protocol.c ../../protocol.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c ../../protocol.c -lm

clean:
	rm -f test_protocol

.PHONY: test clean
