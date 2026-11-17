# Host test of the voice keyboard recognition, needs the emlearn headers
EMLEARN ?= ../../../RIOT/build/pkg/emlearn/emlearn

CFLAGS += -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function
CFLAGS += -I../.. -I$(EMLEARN)
SRC = test_voice.c ../../voice.c ../../keys.c

test: test_voice test_voice_azerty
	./test_voice
	./test_voice_azerty

test_voice: $(SRC) ../../voice.h ../../keys.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

# only checks that the French layout builds and gives other codes
test_voice_azerty: test_azerty.c ../../keys.c ../../keys.h
	$(CC) $(CFLAGS) -DKEYS_AZERTY -o $@ test_azerty.c ../../keys.c

clean:
	rm -f test_voice test_voice_azerty

.PHONY: test clean
