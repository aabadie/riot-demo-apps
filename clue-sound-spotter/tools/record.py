#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Save the sounds recorded on the board, for train.py.

Put the board in record mode, select a sound with B, then make the sound.
The board cuts the event and sends its samples, saved as
data/<SOUND>/<n>.wav, to listen to them. Around 20 of each kind give a good
model.
"""

import argparse
from pathlib import Path

import numpy as np

from sounds import APP_DIR, FRAME, LABELS, next_free, save_wav


def main() -> None:
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--data", type=Path, default=APP_DIR / "tools" / "data")
    args = parser.parse_args()

    counts = {label: len(list((args.data / label).glob("*.wav")))
              for label in LABELS}
    print("waiting for sounds, Ctrl-C to stop")
    with serial.Serial(args.port, args.baudrate, timeout=1) as port:
        try:
            while True:
                line = port.readline().decode("ascii", "replace").strip()
                if not line.startswith("R "):
                    if line:
                        print(f"board: {line}")
                    continue
                fields = line.split()
                if len(fields) != 4 or fields[1] not in LABELS:
                    print("ignored a malformed recording")
                    continue
                try:
                    label, n_frames = fields[1], int(fields[2])
                    pcm = np.frombuffer(bytes.fromhex(fields[3]), dtype="<i2")
                except ValueError:
                    print("ignored a malformed recording")
                    continue
                if len(pcm) != n_frames * FRAME:
                    print(f"ignored a truncated recording of {label}")
                    continue
                folder = args.data / label
                folder.mkdir(parents=True, exist_ok=True)
                path = next_free(folder, ".wav")
                save_wav(path, pcm)
                counts[label] += 1
                print(f"saved {path} ({n_frames * FRAME / 16000:.2f} s), " +
                      ", ".join(f"{k} {v}" for k, v in counts.items()))
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
