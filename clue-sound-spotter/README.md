# Sound Spotter

Sound event recognition with an emlearn random forest, on the Adafruit
CLUE. The board listens with its PDM microphone and recognizes:

| Sound   | Action                         |
|---------|--------------------------------|
| CLAP    | switches the lamp on or off    |
| SNAP    | changes the color of the lamp  |
| WHISTLE | shows its pitch                |
| KNOCK   | counts the knocks              |

## Usage

    make RIOTBASE=<RIOT tree with the demo-apps branch> flash term

## Controls

    A:            lamp off, counters reset
    B:            record mode, then the next sound to record
    B (held 1 s): back to recognition

## Train it on your sounds

The model in `model.h` is trained on synthetic sounds (`tools/sounds.py`):
noise bursts for claps and snaps, tones for whistles, damped low tones for
knocks. Real sounds in your room make a much better model. With the venv
at the top of the repository:

    ../.venv/bin/python tools/record.py      # saves to tools/data/<SOUND>/
    # on the board: B for record mode, B to select a sound, then make it,
    # around 20 times per sound
    ../.venv/bin/python tools/train.py       # writes model.h
    make RIOTBASE=... flash

The recordings are WAV files, cut by the board exactly as it cuts the
events it recognizes.

## How it works

- The audio is analyzed in frames of 16 ms with the emlearn FFT: 16 bands
  from 100 Hz to 8 kHz, the level, the spectral centroid and flatness, and
  the strongest frequency.
- An event starts 12 dB above the noise floor and ends when the level
  stays below the floor plus 6 dB for 6 frames, or after a second. The
  bar at the top shows the level, the floor (grey mark) and the start
  threshold (white mark).
- `sound.c` computes 40 integer features per event: the average and peak
  level of each band relative to the loudest frame, the duration, the time
  to the peak, the decay, the number of onsets, and the statistics of the
  centroid, flatness and strongest frequency.
- A random forest of 20 trees classifies them, below 50% of the votes the
  sound is unknown.
- `train.py` computes the features with `sound.c` itself through ctypes,
  and checks that the exported model agrees with scikit-learn. The
  emlearn headers define non static functions, so all the emlearn code is
  in `sound_eml.c`. The thresholds of the trees get the same `floor(t) + 1`
  fix as in the Spell Caster, see its README.

## Tests

    make -C tests/host

trains on synthetic sounds into `tests/host/out/` and checks the accuracy
and the agreement between C and Python.
