# Spell Caster

Gesture recognition with an emlearn random forest, on the Adafruit CLUE
(accelerometer and gyroscope) and the Adafruit PyBadge (accelerometer).
Hold A and draw a gesture in the air with the board to cast a spell:

| Gesture                       | Spell     |
|-------------------------------|-----------|
| CIRCLE, in the plane of the screen | SHIELD |
| ZIGZAG, left and right        | LIGHTNING |
| FLICK of the wrist, upwards   | FIREBALL  |
| SHAKE, fast                   | FROST     |
| THRUST, forward and back      | BLAST     |

## Usage

    make flash term
    make BOARD=adafruit-pybadge flash term

The default board is `adafruit-clue`.

## Controls

    A (held):        capture a gesture
    B:               record mode, then the next gesture to record
    B (held 1 s):    back to casting
    PyBadge START:   record mode on or off
    PyBadge LEFT/RIGHT: gesture to record
    PyBadge SELECT:  sound on or off

## Train it on your gestures

The model in `model.h` is trained on synthetic gestures, from a rough
physical model of each movement (`tools/gestures.py`). It is enough to try
the application, but a model trained on your own gestures, on your board,
works much better. With the venv at the top of the repository:

    ../.venv/bin/python tools/record.py       # saves to tools/data/<GESTURE>/
    # on the board: B for record mode, B to select a gesture, hold A and
    # do it, around 20 times per gesture
    ../.venv/bin/python tools/train.py        # writes model.h
    make RIOTBASE=... flash

`train.py` prints the accuracy on a quarter of the gestures kept for
testing, of the C model as on the board, and the confusion matrix, then
trains on all of them. `model.h` is replaced only when the checks pass.

## How it works

- The gesture is sampled at 100 Hz while A is held, up to 3 seconds.
- `gesture.c` computes 71 integer features: for each axis of the
  accelerometer and the gyroscope, and for the dynamic acceleration
  |a| - 1 g, the mean, standard deviation, minimum, maximum, the means of
  the 4 quarters of the gesture, the mean change between two samples and
  where the maximum is compared to the minimum; plus the duration.
- A random forest of 20 trees, exported with emlearn, classifies them.
  Below 45% of the votes, the spell fizzles.
- `train.py` computes the features with `gesture.c` itself, through
  ctypes, so the features of the training are exactly the ones of the
  board. After the export, it builds the new model with `gesture_model.c`
  and checks that the C model and scikit-learn agree on at least 98% of
  the gestures: emlearn votes with the class of each tree, scikit-learn
  averages their probabilities, so a tie can go either way.
- emlearn 0.21.1 truncates the thresholds of the trees to integers and
  compares with `<`, where scikit-learn compares with `<=`. The features
  are integers, so `train.py` sets each threshold to `floor(t) + 1` before
  the export.

## Tests

    make -C tests/host

trains on synthetic gestures into `tests/host/out/` and checks the
accuracy and the agreement between C and Python. The synthetic gestures
are easy to tell apart, real ones are not: expect a lower accuracy with
your recordings.
