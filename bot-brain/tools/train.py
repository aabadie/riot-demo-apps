#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Train the neural network of the Pong bot by imitation, export it with
emlearn.

pong.c, the game of the board, runs through ctypes. In each frame, the
scripted player (pong_expert) tells what it would do; the network learns to
do the same from what it sees (pong_features). While the data is
collected, the bot paddle sometimes makes random moves, so that the network
also learns to recover from its own mistakes.

After the export, the network of model.h plays against the scripted player
in C, to compare how many balls it returns per miss with the scripted
player in its place.
"""

import argparse
import ctypes
import os
import subprocess
import sys
import tempfile
import warnings
from pathlib import Path

import numpy as np
from numpy.typing import NDArray
from sklearn.exceptions import ConvergenceWarning
from sklearn.neural_network import MLPClassifier

APP_DIR: Path = Path(__file__).resolve().parent.parent
FEATURES: int = 7
UP, STAY, DOWN = -1, 0, 1


class Pong(ctypes.Structure):
    _fields_ = [("bx", ctypes.c_float), ("by", ctypes.c_float),
                ("vx", ctypes.c_float), ("vy", ctypes.c_float),
                ("left", ctypes.c_float), ("right", ctypes.c_float),
                ("bot_speed", ctypes.c_float), ("hits", ctypes.c_uint),
                ("rng", ctypes.c_uint32)]


def emlearn_module():
    saved = np.geterr()
    import emlearn
    # emlearn switches numpy to raise on any floating point error
    np.seterr(**saved)
    return emlearn


def build(build_dir: Path, model_dir: Path | None) -> ctypes.CDLL:
    sources = [APP_DIR / "pong.c"]
    includes = [f"-I{APP_DIR}"]
    if model_dir is not None:
        sources.append(APP_DIR / "pong_bot.c")
        includes = [f"-I{model_dir}"] + includes + \
            [f"-I{emlearn_module().includedir}"]
    lib = build_dir / ("libpong_bot.so" if model_dir else "libpong.so")
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-Wall", "-Werror",
                    *includes, *map(str, sources), "-o", str(lib), "-lm"],
                   check=True)
    dll = ctypes.CDLL(str(lib))
    dll.pong_init.argtypes = [ctypes.POINTER(Pong), ctypes.c_uint32, ctypes.c_float]
    dll.pong_serve.argtypes = [ctypes.POINTER(Pong), ctypes.c_int]
    dll.pong_step.argtypes = [ctypes.POINTER(Pong), ctypes.c_int, ctypes.c_int]
    dll.pong_expert.argtypes = [ctypes.POINTER(Pong), ctypes.c_int]
    dll.pong_features.argtypes = [ctypes.POINTER(Pong),
                                  ctypes.POINTER(ctypes.c_float)]
    if model_dir is not None:
        dll.pong_bot.argtypes = [ctypes.POINTER(Pong),
                                 ctypes.POINTER(ctypes.c_float)]
    return dll


def collect(lib: ctypes.CDLL, games: int, frames: int,
            rng: np.random.Generator) -> tuple[NDArray[np.float32], NDArray[np.int64]]:
    xs: list[NDArray[np.float32]] = []
    ys: list[int] = []
    feats = (ctypes.c_float * FEATURES)()
    game = Pong()
    for n in range(games):
        lib.pong_init(ctypes.byref(game), int(rng.integers(1, 2**31)),
                      float(rng.uniform(1.0, 2.0)))
        explore = rng.uniform(0.05, 0.35)
        for _ in range(frames):
            left = lib.pong_expert(ctypes.byref(game), -1)
            if rng.random() < 0.15:
                left = int(rng.integers(-1, 2))
            target = lib.pong_expert(ctypes.byref(game), 1)
            lib.pong_features(ctypes.byref(game), feats)
            xs.append(np.frombuffer(feats, dtype=np.float32).copy())
            ys.append(target)
            right = target if rng.random() > explore else int(rng.integers(-1, 2))
            point = lib.pong_step(ctypes.byref(game), left, right)
            if point:
                lib.pong_serve(ctypes.byref(game), -point)
    return np.array(xs), np.array(ys) + 1   # classes 0, 1, 2


def match(lib: ctypes.CDLL, use_bot: bool, misses: int, seed: int,
          bot_speed: float) -> float:
    """Balls returned by the right paddle per ball missed, against the
    scripted player with a few mistakes on the left. The ball speeds up at
    each hit, so every rally ends."""
    rng = np.random.default_rng(seed)
    game = Pong()
    lib.pong_init(ctypes.byref(game), seed, bot_speed)
    returned = missed = 0
    while missed < misses and returned < 100000:
        left = lib.pong_expert(ctypes.byref(game), -1)
        if rng.random() < 0.15:
            left = int(rng.integers(-1, 2))
        if use_bot:
            right = lib.pong_bot(ctypes.byref(game), None)
        else:
            right = lib.pong_expert(ctypes.byref(game), 1)
        was_coming = game.vx > 0
        point = lib.pong_step(ctypes.byref(game), left, right)
        if was_coming and game.vx < 0:
            returned += 1
        if point == 1:
            missed += 1
        if point:
            lib.pong_serve(ctypes.byref(game), -point)
    return returned / max(missed, 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games", type=int, default=150)
    parser.add_argument("--frames", type=int, default=2000)
    parser.add_argument("--hidden", type=int, default=16)
    parser.add_argument("--output", type=Path, default=APP_DIR / "model.h")
    parser.add_argument("--min-returns", type=float, default=0.0,
                        help="exit with an error when the bot returns fewer "
                             "balls per miss at any speed")
    args = parser.parse_args()
    output = args.output.resolve()
    rng = np.random.default_rng(1)

    with tempfile.TemporaryDirectory() as tmp:
        lib = build(Path(tmp), None)
        x, y = collect(lib, args.games, args.frames, rng)
    print(f"{len(y)} frames, up {np.mean(y == 0):.0%}, stay "
          f"{np.mean(y == 1):.0%}, down {np.mean(y == 2):.0%}")

    model = MLPClassifier(hidden_layer_sizes=(args.hidden,), max_iter=60,
                          early_stopping=True, random_state=1)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", ConvergenceWarning)
        model.fit(x, y)
    print(f"the network does what the scripted player does in "
          f"{model.best_validation_score_:.1%} of the frames")

    emlearn = emlearn_module()
    with tempfile.TemporaryDirectory() as tmp:
        cwd = os.getcwd()
        os.chdir(tmp)
        try:
            emlearn.convert(model, method="inline").save(file=str(output),
                                                         name="pong_model")
        finally:
            os.chdir(cwd)
    params = sum(w.size for w in model.coefs_) + sum(b.size for b in model.intercepts_)
    print(f"wrote {output}: {params} parameters")

    worst = float("inf")
    with tempfile.TemporaryDirectory() as tmp:
        lib = build(Path(tmp), output.parent)
        for speed in (1.0, 1.3, 1.6):
            expert = match(lib, False, 30, 7, speed)
            bot = match(lib, True, 30, 7, speed)
            worst = min(worst, bot)
            print(f"bot speed {speed}: the network returns {bot:.1f} balls "
                  f"per miss, the scripted player {expert:.1f}")
    return 0 if worst >= args.min_returns else 1


if __name__ == "__main__":
    sys.exit(main())
