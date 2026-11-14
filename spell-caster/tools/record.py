#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Save the gestures recorded on the board, for train.py.

Put the board in record mode, select a gesture, then hold A while doing it.
Each gesture is saved in data/<GESTURE>/<n>.csv. Around 20 of each kind
give a good model.
"""

import argparse
from pathlib import Path

from gestures import APP_DIR, LABELS, save_recording


def main() -> None:
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--data", type=Path, default=APP_DIR / "tools" / "data")
    args = parser.parse_args()

    counts = {label: len(list((args.data / label).glob("*.csv")))
              for label in LABELS}
    print("waiting for gestures, Ctrl-C to stop")
    with serial.Serial(args.port, args.baudrate, timeout=1) as port:
        try:
            while True:
                line = port.readline().decode("ascii", "replace").strip()
                if not line.startswith("R "):
                    if line:
                        print(f"board: {line}")
                    continue
                fields = line.split()
                label, n = fields[1], int(fields[2])
                values = [int(v) for v in fields[3:]]
                if label not in LABELS or len(values) != n * 6:
                    print(f"ignored a malformed recording of {label}")
                    continue
                path = save_recording(args.data, label, values)
                counts[label] += 1
                print(f"saved {path} ({n} samples), " +
                      ", ".join(f"{k} {v}" for k, v in counts.items()))
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
