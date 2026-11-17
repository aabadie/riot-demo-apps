# Voice Keyboard

The Adafruit CLUE as a USB keyboard you spell to. Teach it your voice saying
the NATO alphabet (ALFA, BRAVO, CHARLIE...), the digits (ZERO to NINE) and
four commands, then spell: the letters are typed on the computer.

| Word                 | Key typed                          |
|----------------------|------------------------------------|
| ALFA to ZULU         | a to z                             |
| ZERO to NINE         | 0 to 9                             |
| SPACE                | space                              |
| ENTER                | enter                              |
| DELETE               | backspace                          |
| SHIFT                | the next letter is upper case      |

## Why it is taught, and why the NATO alphabet

A model that understands anyone saying any letter needs a convolutional
network trained on thousands of speakers, and there is no such dataset for
spelled letters. Single letters are also very hard to tell apart: B, D, E,
G, P, T and V differ only by a short consonant. The NATO words are long and
distinct, made to be understood over a bad radio link.

So the board learns your voice: 3 examples of each word, 120 utterances,
about 5 minutes. Each utterance becomes up to 40 frames of 20 log mel bands
(emlearn FFT and mel filterbank, every 32 ms), kept as 8 bit numbers: 96
KiB for the 120 examples. A new utterance is compared to all of them with
dynamic time warping, restricted to a band around the diagonal, and the
closest word wins when it is close enough and clearly closer than the
second one. Otherwise the board shows "?" and types nothing.

The examples are saved in flash after each one, so they survive a reset.
The flash pages are not part of the firmware image, so they may also survive
flashing a new firmware, depending on what the bootloader erases (not
checked yet). A store with another layout is ignored at start-up.

## Usage

    make flash

The board appears as a USB keyboard and a USB serial port at the same
time. A USB keyboard sends key positions, and the computer turns them into
characters with its own layout. For a computer set to a French layout:

    make KEYMAP=azerty flash

The device uses the testing USB IDs of RIOT (`USB_VID_TESTING`,
`USB_PID_TESTING`); set `USB_VID` and `USB_PID` to use other ones.

## Controls

Keyboard mode:

    A: typing on or off (the words are still recognized and shown)
    B: teach mode

Teach mode (say the word on the screen 3 times, the board then moves on to
the next word):

    A:                      next word
    A (held for a second):  previous word
    B:                      back to the keyboard mode

Holding A and B together for 3 seconds forgets all the words.

The board starts in teach mode when it knows no word. Words that were
never taught are ignored, so teaching only the letters of a few words is
enough for a first try.

The grid shows the 40 words, colored by their number of examples (grey:
none, orange: 1 or 2, green: 3); the word recognized is framed in yellow.
The bar under the word is the sound level, red during an utterance.

## Serial output

    K BRAVO b                            word recognized and key
    voice: word 1 at 0.912 (limit 1.850), then word 16 at 1.402: accepted
    voice: example 3 of word 1, 21 features
    saved, 1 pages written

The distances help to tune `VOICE_SPREAD_FACTOR` and `VOICE_MARGIN` in
`voice.h` when words are rejected or confused.

## Tips

- Speak 20 to 40 cm from the board, the microphone is the small hole above
  the screen.
- Teach in the place where you will use it, with the same background noise.
- Pause between words: an utterance ends after 256 ms of silence.
- With 40 words taught, recognizing a word should take about 0.1 s (an
  estimate from the host test, not measured), the next word can be spoken
  right away.
- When a word is often confused, teach it again: new examples replace the
  oldest ones.

## Host test

    make -C tests/host

Forty synthetic words made of syllables (harmonic sweeps and noise bursts)
are taught 3 times each, then played with random changes of length, pitch,
loudness and noise. The test checks the recognition rate, the rejection of
a sound that was never taught, that the examples survive a save and a load,
and the key codes of both layouts. It shows that the processing works, not
how well it recognizes real voices.

## Hardware notes

Not tested on hardware yet. Things to check first:

- the keyboard and the serial port both enumerate (`lsusb`, `dmesg`)
- the recognition time with 40 words taught
- the examples survive a reset (`N words loaded from flash` at start-up)
