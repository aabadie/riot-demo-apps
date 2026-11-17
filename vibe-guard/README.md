# Vibe Guard

Vibration anomaly detection learned on the device, on the Adafruit CLUE and
the Adafruit PyBadge. Put the board on a running machine (a fan, a washing
machine, a 3D printer): it learns the normal vibrations for 30 seconds, then
shows how unusual the vibration is and raises an alarm when it stays
unusual. Stop the fan, unbalance it with a piece of tape, or knock on the
table to try it.

## Usage

    make flash term
    make BOARD=adafruit-pybadge flash term

The default board is `adafruit-clue`.

## Controls

    A (or START): learn again
    B (or SELECT): sound on or off

## How it works

- The accelerometer is sampled at 200 Hz, the CLUE uses its LSM6DS, the
  PyBadge its LIS3DH.
- Every 0.32 s, a window of 0.64 s is analyzed with the emlearn FFT: the
  power of the vibration in 8 bands from 2 to 100 Hz, summed over the three
  axes, in dB. The bars show these bands, the white marks the learned
  average.
- During the learning phase, a Gaussian mixture of 3 components is fitted
  on the windows with expectation maximization, on the device.
- emlearn then scores each window against the mixture. The level is how
  far below the typical log-likelihood of the learned windows the window
  is; 1 is the spread of the learned windows. Three windows in a row at
  3 or more raise the alarm.
- Each window is printed as `W <level> <band powers in dB>`.

## Tests

    make -C tests/host

learns a synthetic machine running at about 30 Hz, then checks that the
same machine stays quiet while a stronger vibration, a whine at 85 Hz, an
unbalance at 8 Hz, knocking, and the machine stopping raise the alarm.
