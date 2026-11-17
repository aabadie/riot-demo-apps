# Color Sniffer

Colors taught on the Adafruit CLUE and recognized with an emlearn Gaussian
naive Bayes model, trained on the device. Hold an object (a candy, a LEGO
brick, a fruit) against the color sensor, above the screen, teach up to six
colors, and the board recognizes them and plays a note for each: a color
piano.

## Usage

    make flash term

## Controls

    A (held):     teach the color under the sensor to the selected slot
    B:            select the next slot
    B (held 1 s): forget the selected slot

## How it works

- The APDS9960 gives red, green, blue and clear counts, and a proximity
  reading: colors are only read when an object is close.
- The features are the share of red and of green in red + green + blue,
  which do not depend much on the distance, and the brightness.
- Each slot keeps the running mean and variance of its samples. After each
  sample, they become the summaries of an emlearn naive Bayes model of the
  taught slots, emlearn picks the most likely slot.
- A reading further than 3.5 standard deviations on average from the
  picked slot is unknown.
- The board switches on the two white LEDs next to the sensor (P0.10),
  for a steady light. P0.10 is also an NFC pin of the nRF52840: the LEDs
  only light up when the NFC pins are configured as GPIOs, which the
  Adafruit bootloader usually does. The APDS9960 interrupt pin, P0.09, is
  in the same case.

## Tests

    make -C tests/host

teaches six simulated colors, at varying distances and with sensor noise,
then checks that new readings of them are recognized and that colors that
were not taught are rejected.
