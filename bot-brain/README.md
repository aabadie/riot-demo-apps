# Bot Brain

Pong against a neural network, on the Adafruit PyBadge and the Adafruit
CLUE. The right paddle is played by a small emlearn network (7 inputs, 16
hidden neurons, 3 outputs), trained in Python by imitating a scripted
player in a simulation of this very game. The bars show what the network
wants to do, and on the CLUE, the 16 squares show its hidden neurons.

First to 7 points wins. Win, and the bot gets faster: EASY, MEDIUM, HARD.

## Usage

    make flash
    make BOARD=adafruit-clue flash

The default board is `adafruit-pybadge`.

## Controls

    PyBadge: UP and DOWN move the paddle, A starts, SELECT toggles the sound
    CLUE:    A moves up, B moves down, A or B starts

## How it works

- `pong.c` holds the rules, the inputs of the network (ball position and
  speed, paddle position, distance to the ball, paddle speed), and the
  scripted player: it computes where the ball will arrive, bounces
  included, and goes there.
- `tools/train.py` runs `pong.c` through ctypes: in each frame, the
  scripted player tells what it would do, and the network learns to do the
  same from what it sees. While collecting the frames, the bot paddle
  sometimes moves at random, so that the network also sees, and learns to
  recover from, positions the scripted player would never be in.
- After the export, the network of `model.h` plays against the scripted
  player in C. It does what the scripted player would do in about 90% of
  the frames, which makes it beatable: it returns about 7 balls per miss
  at the EASY and MEDIUM speeds and about 15 at HARD, where the scripted
  player itself returns 10, 30 and thousands. As on the board, the ball
  is served towards the side that lost the point.

## Training

    ../.venv/bin/python tools/train.py              # writes model.h
    ../.venv/bin/python tools/train.py --hidden 32  # a sharper bot

## Tests

    make -C tests/host

trains the network into `tests/host/out/` and checks that it returns at
least 3 balls per miss at each speed.
