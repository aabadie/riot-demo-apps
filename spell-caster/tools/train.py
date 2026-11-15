#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Train the gesture classifier and export it with emlearn.

The gestures recorded with record.py are in data/ by default. Without
recordings, or with --synthetic, the model is trained on synthetic gestures
instead, which is only good enough to try the application.

The features are computed by gesture.c, the code of the board, through
ctypes. After the export, model.h is built with gesture_model.c and run on
every gesture to check that the board and scikit-learn agree.
"""

import argparse
import math
import os
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import confusion_matrix
from sklearn.model_selection import train_test_split

from gestures import (APP_DIR, LABELS, FeatureLib, load_dataset,
                      synthetic_dataset)


# the votes of emlearn and scikit-learn can differ on a tie, more than a few
# differences is an export bug
MIN_AGREEMENT = 0.98


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
            cmodel.save(file=str(output), name="gesture_model")
        finally:
            os.chdir(cwd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=APP_DIR / "tools" / "data")
    parser.add_argument("--synthetic", action="store_true",
                        help="train on synthetic gestures")
    parser.add_argument("--trees", type=int, default=20)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--output", type=Path, default=APP_DIR / "model.h")
    parser.add_argument("--min-accuracy", type=float, default=0.0,
                        help="exit with an error below this test accuracy")
    args = parser.parse_args()

    if args.synthetic or not args.data.is_dir():
        if not args.synthetic:
            print(f"no recordings in {args.data}, using synthetic gestures")
        gestures, labels = synthetic_dataset(per_class=120, seed=1)
    else:
        gestures, labels = load_dataset(args.data)
    counts = {LABELS[i]: labels.count(i) for i in range(len(LABELS))}
    print("gestures: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    if min(counts.values()) < 5:
        sys.exit("record at least 5 gestures of each kind")

    with tempfile.TemporaryDirectory() as tmp:
        lib = FeatureLib(with_model=False, build_dir=Path(tmp))
        x = np.array([lib.features(g) for g in gestures])
    y = np.array(labels)

    x_train, x_test, y_train, y_test = train_test_split(
        x, y, test_size=0.25, random_state=1, stratify=y)
    model = RandomForestClassifier(n_estimators=args.trees,
                                   max_depth=args.depth, random_state=1)
    model.fit(x_train, y_train)
    with tempfile.TemporaryDirectory() as tmp:
        # the accuracy of the C model, the one of the board: emlearn votes
        # with the classes of the leaves, scikit-learn averages probabilities
        header = Path(tmp) / "model.h"
        export(model, header)
        lib = FeatureLib(with_model=True, build_dir=Path(tmp), model_header=header)
        board_test = np.array([lib.classify(f)[0] for f in x_test])
    accuracy = float(np.mean(board_test == y_test))
    print(f"test accuracy {accuracy:.3f} on the board, "
          f"{model.score(x_test, y_test):.3f} in scikit-learn")
    print("confusion matrix (rows: gesture, columns: prediction)")
    print("        " + " ".join(f"{n[:6]:>6}" for n in LABELS))
    for name, row in zip(LABELS, confusion_matrix(y_test, board_test)):
        print(f"{name[:6]:>6}  " + " ".join(f"{v:6d}" for v in row))

    # the final model learns from all the gestures, it replaces the output
    # only if the checks pass
    model.fit(x, y)
    nodes = sum(e.tree_.node_count for e in model.estimators_)
    with tempfile.TemporaryDirectory() as tmp:
        header = Path(tmp) / "model.h"
        export(model, header)
        lib = FeatureLib(with_model=True, build_dir=Path(tmp), model_header=header)
        board = np.array([lib.classify(f)[0] for f in x])
        agree = np.mean(board == model.predict(x))
        print(f"the C model agrees with scikit-learn on {agree:.2%} of the gestures")
        if accuracy < args.min_accuracy or agree < MIN_AGREEMENT:
            print(f"{args.output} not written: accuracy below {args.min_accuracy} "
                  f"or agreement below {MIN_AGREEMENT:.0%}")
            return 1
        shutil.copyfile(header, args.output)
    print(f"wrote {args.output}: {args.trees} trees, {nodes} nodes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
