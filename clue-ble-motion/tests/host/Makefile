# Host test of the orientation filter, and of the packets decoded with the
# protocol.js of the page (needs node)
CFLAGS += -O2 -Wall -Wextra -Werror -I../..

test: test_fusion test_protocol
	./test_fusion
	./test_protocol | node decode.mjs

test_fusion: test_fusion.c ../../fusion.c ../../fusion.h
	$(CC) $(CFLAGS) -o $@ test_fusion.c -lm

test_protocol: test_protocol.c ../../protocol.c ../../protocol.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c ../../protocol.c -lm

clean:
	rm -f test_fusion test_protocol

.PHONY: test clean
