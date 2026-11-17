# Disco Digit Draw

Draw a digit with a finger on the touch screen of the STM32F723E-DISCO:
emlearn recognizes it, on the board, with the network of
[Digit Oracle](../digit-oracle) trained on MNIST.

The digit is recognized while it is drawn, 5 times per second, and again
when the finger lifts. The screen shows the digit, its probability and the
probability of each digit; the serial port prints the digit and its
probability when the finger lifts:

    digit 7 (93 %)

## Usage

    make flash term

The default board is `stm32f723e-disco`. To start a new drawing, tap
CLEAR, press the blue user button, or touch the canvas again 2 seconds
after the last stroke. Digits with several strokes (4, 5, 7 with a bar)
are fine as long as the strokes follow each other within 2 seconds.

## How it works

- The canvas has the 28x28 pixels of an MNIST digit, 6 screen pixels each.
  A stroke paints a soft brush along the finger, interpolated between the
  touch readings (50 per second), so fast strokes stay continuous.
- `digits.c` builds the digit preparation and the network of Digit Oracle
  from its sources: the drawing is scaled to fit a 20x20 box and centered
  on its center of mass, like the MNIST digits, reduced to 14x14 and
  classified by a network of 64 hidden neurons (51 KiB of flash). See the
  README of Digit Oracle for the training.

## Tips

- Draw large, filling most of the canvas, like a digit on paper.
- The MNIST writers were American: a 1 drawn with a base and a flag, or
  a 7 with a middle bar, are rarer in the training set and may be
  recognized less reliably.
