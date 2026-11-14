# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""MNIST loading and the preprocessing shared with the board."""

import numpy as np
from numpy.typing import NDArray

SIZE: int = 28
POOL: int = 2
FEATURES: int = (SIZE // POOL) ** 2


def load_mnist() -> tuple[NDArray[np.uint8], NDArray[np.int64]]:
    """Return the 70000 MNIST digits as 28x28 uint8 images and labels.

    The first 60000 are the training set, the last 10000 the test set. The
    dataset is downloaded from OpenML once and cached by scikit-learn in
    ~/scikit_learn_data.
    """
    from sklearn.datasets import fetch_openml

    x, y = fetch_openml("mnist_784", version=1, return_X_y=True,
                        as_frame=False, parser="liac-arff")
    return x.astype(np.uint8).reshape(-1, SIZE, SIZE), y.astype(np.int64)


def features(images: NDArray[np.uint8]) -> NDArray[np.float32]:
    """Average 2x2 pooling to 14x14, scaled to 0..1, as digits.c does."""
    n = images.shape[0]
    pooled = images.reshape(n, SIZE // POOL, POOL, SIZE // POOL, POOL)
    pooled = pooled.astype(np.float32).sum(axis=(2, 4)) / (POOL * POOL * 255.0)
    return pooled.reshape(n, FEATURES)
