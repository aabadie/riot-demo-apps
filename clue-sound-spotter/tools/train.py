#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Train the sound classifier and export it with emlearn.

The sounds recorded with record.py are in data/ by default. Without
recordings, or with --synthetic, the model is trained on synthetic sounds
instead, which is only good enough to try the application.

The features are computed by sound.c, the code of the board, through
ctypes. After the export, model.h is built with sound_eml.c and run on
every sound to check that the board and scikit-learn agree.
"""

import argparse
import math
import os
import sys
import tempfile
from pathlib import Path

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import confusion_matrix
from sklearn.model_selection import train_test_split

from sounds import APP_DIR, LABELS, SoundLib, load_dataset, synthetic_dataset


def export(model: RandomForestClassifier, output: Path) -> None:
    saved = np.geterr()
    import emlearn
    np.seterr(**saved)

    cmodel = emlearn.convert(model, method="loadable")
    # emlearn truncates the thresholds to integers and compares with "<",
    # scikit-learn compares with "<=". The features are integers, so
    # "x <= threshold" is "x < floor(threshold) + 1".
    nodes, _, _ = cmodel.forest_
    for node in nodes:
        node[1] = math.floor(node[1]) + 1
    with tempfile.TemporaryDirectory() as tmp:
        cwd = os.getcwd()
        os.chdir(tmp)
        try:
            cmodel.save(file=str(output), name="sound_model")
        finally:
            os.chdir(cwd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=APP_DIR / "tools" / "data")
    parser.add_argument("--synthetic", action="store_true",
                        help="train on synthetic sounds")
    parser.add_argument("--trees", type=int, default=20)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--output", type=Path, default=APP_DIR / "model.h")
    parser.add_argument("--min-accuracy", type=float, default=0.0,
                        help="exit with an error below this test accuracy")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        lib = SoundLib(with_model=False, build_dir=Path(tmp))
        if args.synthetic or not args.data.is_dir():
            if not args.synthetic:
                print(f"no recordings in {args.data}, using synthetic sounds")
            clips, clip_labels = synthetic_dataset(per_class=100, seed=1)
            events, labels = [], []
            missed = 0
            for clip, label in zip(clips, clip_labels):
                event = lib.find_event(clip)
                if event is None:
                    missed += 1
                    continue
                events.append(event)
                labels.append(label)
            print(f"{len(events)} events detected in {len(clips)} clips")
        else:
            events, labels = load_dataset(args.data)
        rows, kept = [], []
        for event, label in zip(events, labels):
            feats = lib.features(event)
            if feats is not None:
                rows.append(feats)
                kept.append(label)
    x = np.array(rows)
    y = np.array(kept)
    counts = {LABELS[i]: int(np.sum(y == i)) for i in range(len(LABELS))}
    print("sounds: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    if min(counts.values()) < 5:
        sys.exit("record at least 5 sounds of each kind")

    x_train, x_test, y_train, y_test = train_test_split(
        x, y, test_size=0.25, random_state=1, stratify=y)
    model = RandomForestClassifier(n_estimators=args.trees,
                                   max_depth=args.depth, random_state=1)
    model.fit(x_train, y_train)
    accuracy = model.score(x_test, y_test)
    print(f"test accuracy {accuracy:.3f}")
    print("confusion matrix (rows: sound, columns: prediction)")
    print("         " + " ".join(f"{n:>7}" for n in LABELS))
    for name, row in zip(LABELS, confusion_matrix(y_test, model.predict(x_test))):
        print(f"{name:>8} " + " ".join(f"{v:7d}" for v in row))

    model.fit(x, y)
    export(model, args.output.resolve())
    nodes = sum(e.tree_.node_count for e in model.estimators_)
    print(f"wrote {args.output}: {args.trees} trees, {nodes} nodes")

    with tempfile.TemporaryDirectory() as tmp:
        lib = SoundLib(with_model=True, build_dir=Path(tmp),
                       model_dir=args.output.resolve().parent)
        board = np.array([lib.classify(f) for f in x])
    agree = np.mean(board == model.predict(x))
    print(f"the C model agrees with scikit-learn on {agree:.2%} of the sounds")
    return 0 if agree == 1.0 and accuracy >= args.min_accuracy else 1


if __name__ == "__main__":
    sys.exit(main())
