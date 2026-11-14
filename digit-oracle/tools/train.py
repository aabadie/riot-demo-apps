#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Train the digit classifier on MNIST and export it with emlearn.

The model is a multi-layer perceptron with one hidden layer, taking the
14x14 pooled image. It is trained on the MNIST digits and on binarized
copies of them, closer to the digits drawn on the board.
"""

import argparse
import os
import tempfile
import time
import warnings
from pathlib import Path

import numpy as np
from sklearn.exceptions import ConvergenceWarning
from sklearn.neural_network import MLPClassifier

from mnist import features, load_mnist

HERE: Path = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hidden", type=int, default=64,
                        help="neurons of the hidden layer (default 64)")
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--output", type=Path,
                        default=HERE.parent / "model.h")
    args = parser.parse_args()

    images, labels = load_mnist()
    train, test = slice(0, 60000), slice(60000, None)
    binarized = np.where(images > 100, 255, 0).astype(np.uint8)

    x_train = np.vstack([features(images[train]), features(binarized[train])])
    y_train = np.concatenate([labels[train], labels[train]])

    model = MLPClassifier(hidden_layer_sizes=(args.hidden,),
                          max_iter=args.epochs, early_stopping=True,
                          random_state=1)
    start = time.monotonic()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", ConvergenceWarning)
        model.fit(x_train, y_train)
    print(f"trained in {time.monotonic() - start:.0f} s")

    accuracy = model.score(features(images[test]), labels[test])
    accuracy_bin = model.score(features(binarized[test]), labels[test])
    print(f"test accuracy {accuracy:.4f}, binarized {accuracy_bin:.4f}")

    # emlearn switches numpy to raise on any floating point error when
    # imported, which breaks the training, so import it only now
    import emlearn

    output = args.output.resolve()
    # emlearn writes temporary files in the current directory
    with tempfile.TemporaryDirectory() as tmp:
        cwd = os.getcwd()
        os.chdir(tmp)
        try:
            cmodel = emlearn.convert(model, method="inline")
            cmodel.save(file=str(output), name="digits_model")
        finally:
            os.chdir(cwd)
    weights = sum(w.size for w in model.coefs_) + \
        sum(b.size for b in model.intercepts_)
    print(f"wrote {output}: {weights} parameters, "
          f"{weights * 4 // 1024} KiB of flash")


if __name__ == "__main__":
    main()
