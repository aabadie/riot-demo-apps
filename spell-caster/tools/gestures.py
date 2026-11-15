# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Gestures of the Spell Caster: labels, datasets, and the C feature code of
the board called through ctypes."""

import csv
import ctypes
import math
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

LABELS: list[str] = ["CIRCLE", "ZIGZAG", "FLICK", "SHAKE", "THRUST"]
RATE_HZ: int = 100
CHANNELS: int = 6
APP_DIR: Path = Path(__file__).resolve().parent.parent

Gesture = NDArray[np.float32]   # n x 6: ax ay az in g, gx gy gz in dps


def emlearn_include() -> str:
    import numpy

    # emlearn switches numpy to raise on any floating point error when
    # imported, restore the previous behavior
    saved = numpy.geterr()
    import emlearn
    numpy.seterr(**saved)
    return emlearn.includedir


class FeatureLib:
    """gesture.c, and gesture_model.c with model.h, built as a shared
    library."""

    def __init__(self, with_model: bool, build_dir: Path,
                 model_header: Path = APP_DIR / "model.h") -> None:
        sources = [APP_DIR / "gesture.c"]
        if with_model:
            sources.append(APP_DIR / "gesture_model.c")
        lib = build_dir / ("libgesture_model.so" if with_model
                           else "libgesture.so")
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-Wall", "-Werror",
                        f'-DMODEL_HEADER="{model_header}"', f"-I{APP_DIR}",
                        f"-I{emlearn_include()}",
                        *map(str, sources), "-o", str(lib), "-lm"],
                       check=True)
        self.lib = ctypes.CDLL(str(lib))
        self.n_features = 7 * 10 + 1
        self.n_classes = len(LABELS)

    def features(self, gesture: Gesture) -> NDArray[np.int16]:
        samples = np.ascontiguousarray(gesture, dtype=np.float32)
        out = np.zeros(self.n_features, dtype=np.int16)
        ret = self.lib.gesture_features(
            samples.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            ctypes.c_uint(len(samples)),
            out.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)))
        if ret != 0:
            raise ValueError("gesture too short")
        return out

    def classify(self, features: NDArray[np.int16]) -> tuple[int, NDArray[np.float32]]:
        feats = np.ascontiguousarray(features, dtype=np.int16)
        proba = np.zeros(self.n_classes, dtype=np.float32)
        best = self.lib.gesture_classify(
            feats.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
            proba.ctypes.data_as(ctypes.POINTER(ctypes.c_float)))
        return best, proba


def build_dir() -> tempfile.TemporaryDirectory:
    return tempfile.TemporaryDirectory(prefix="spell-caster-")


def from_raw(values: list[int]) -> Gesture:
    """Raw integers of the board (milli-g, tenths of dps) to a gesture, with
    the same float operations as the board."""
    raw = np.array(values, dtype=np.float32).reshape(-1, CHANNELS)
    out = np.empty_like(raw)
    out[:, :3] = raw[:, :3] / np.float32(1000.0)
    out[:, 3:] = raw[:, 3:] / np.float32(10.0)
    return out


def load_dataset(directory: Path) -> tuple[list[Gesture], list[int]]:
    """Load the recordings of record.py: <directory>/<LABEL>/<n>.csv"""
    gestures: list[Gesture] = []
    labels: list[int] = []
    for index, label in enumerate(LABELS):
        for path in sorted((directory / label).glob("*.csv")):
            with path.open() as f:
                rows = [int(v) for row in csv.reader(f) if row and
                        not row[0].startswith("a") for v in row]
            gestures.append(from_raw(rows))
            labels.append(index)
    return gestures, labels


def next_free(folder: Path, suffix: str) -> Path:
    """The file after the highest numbered one, a deleted recording leaves a
    gap but nothing is overwritten."""
    numbers = [int(p.stem) for p in folder.glob(f"*{suffix}") if p.stem.isdigit()]
    return folder / f"{max(numbers, default=-1) + 1:04d}{suffix}"


def save_recording(directory: Path, label: str, values: list[int]) -> Path:
    folder = directory / label
    folder.mkdir(parents=True, exist_ok=True)
    path = next_free(folder, ".csv")
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["ax", "ay", "az", "gx", "gy", "gz"])
        for i in range(0, len(values), CHANNELS):
            writer.writerow(values[i:i + CHANNELS])
    return path


def _rotation(rng: np.random.Generator, max_deg: float) -> NDArray[np.float64]:
    a, b, c = np.radians(rng.uniform(-max_deg, max_deg, 3))
    rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)],
                   [0, math.sin(a), math.cos(a)]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0],
                   [-math.sin(b), 0, math.cos(b)]])
    rz = np.array([[math.cos(c), -math.sin(c), 0],
                   [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return rz @ ry @ rx


def _pulse(t: NDArray[np.float64], center: float, width: float) -> NDArray[np.float64]:
    return np.exp(-0.5 * ((t - center) / width) ** 2)


def synthesize(label: int, rng: np.random.Generator) -> Gesture:
    """A rough model of each gesture, board face up, for a default model
    and the tests. Real recordings make a much better model."""
    name = LABELS[label]
    lin = None
    gyro = None
    if name == "CIRCLE":
        period = rng.uniform(0.6, 1.0)
        turns = rng.uniform(1.0, 1.6)
        dur = period * turns
        t = np.arange(int(dur * RATE_HZ)) / RATE_HZ
        w = 2 * math.pi / period * rng.choice([-1, 1])
        amp = rng.uniform(0.15, 0.25) * w * w / 9.81
        env = np.minimum(1, np.minimum(t, dur - t) / 0.15)
        lin = np.stack([-amp * np.cos(w * t), -amp * np.sin(w * t),
                        np.zeros_like(t)], axis=1) * env[:, None]
        gyro = np.stack([np.zeros_like(t), np.zeros_like(t),
                         0.15 * np.degrees(w) * env], axis=1)
    elif name == "ZIGZAG":
        freq = rng.uniform(1.8, 3.0)
        strokes = rng.integers(3, 6)
        dur = strokes / (2 * freq)
        t = np.arange(int(dur * RATE_HZ)) / RATE_HZ
        s = np.sin(2 * math.pi * freq * t)
        amp = rng.uniform(1.0, 2.0)
        lin = np.stack([amp * np.sign(s) * np.abs(s) ** 0.5,
                        -0.3 * np.sin(4 * math.pi * freq * t),
                        np.zeros_like(t)], axis=1)
        gyro = np.stack([np.zeros_like(t), np.zeros_like(t),
                         40 * np.cos(2 * math.pi * freq * t)], axis=1)
    elif name == "FLICK":
        dur = rng.uniform(0.35, 0.6)
        t = np.arange(int(dur * RATE_HZ)) / RATE_HZ
        c = dur * rng.uniform(0.35, 0.5)
        width = rng.uniform(0.04, 0.07)
        peak = rng.uniform(400, 700)
        gx = peak * _pulse(t, c, width) - 0.35 * peak * _pulse(t, c + 2.5 * width, width)
        lin = np.stack([np.zeros_like(t),
                        -0.6 * _pulse(t, c, width),
                        rng.uniform(1.2, 2.0) * _pulse(t, c - width, width)
                        - 0.8 * _pulse(t, c + 2 * width, width)], axis=1)
        gyro = np.stack([gx, np.zeros_like(t), np.zeros_like(t)], axis=1)
    elif name == "SHAKE":
        freq = rng.uniform(5.0, 8.0)
        dur = rng.uniform(0.8, 1.5)
        t = np.arange(int(dur * RATE_HZ)) / RATE_HZ
        amp = rng.uniform(1.5, 3.0)
        env = np.minimum(1, np.minimum(t, dur - t) / 0.1)
        lin = np.stack([amp * np.sin(2 * math.pi * freq * t) * env,
                        0.2 * amp * np.sin(2 * math.pi * freq * t + 1) * env,
                        np.zeros_like(t)], axis=1)
        gyro = np.stack([np.zeros_like(t), np.zeros_like(t),
                         60 * np.sin(2 * math.pi * freq * t) * env], axis=1)
    elif name == "THRUST":
        dur = rng.uniform(0.4, 0.7)
        t = np.arange(int(dur * RATE_HZ)) / RATE_HZ
        amp = rng.uniform(1.5, 2.5)
        width = dur * 0.12
        lin = np.stack([np.zeros_like(t),
                        amp * _pulse(t, dur * 0.3, width)
                        - amp * _pulse(t, dur * 0.7, width),
                        np.zeros_like(t)], axis=1)
        gyro = np.zeros((len(t), 3))
    assert lin is not None and gyro is not None

    # the button is pressed a bit before and released a bit after
    before = np.zeros((int(rng.uniform(0.05, 0.25) * RATE_HZ), 3))
    after = np.zeros((int(rng.uniform(0.05, 0.25) * RATE_HZ), 3))
    lin = np.vstack([before, lin, after])
    gyro = np.vstack([before, gyro, after])

    rot = _rotation(rng, 20)
    gravity = rot @ np.array([0.0, 0.0, 1.0])
    acc = lin @ rot.T + gravity + rng.normal(0, 0.02, lin.shape)
    gyr = gyro @ rot.T + rng.normal(0, 1.0, gyro.shape)
    if rng.random() < 0.5:
        # half of them without gyroscope, like on the PyBadge
        gyr[:] = 0
    # quantized like the board readings
    raw = np.hstack([np.round(acc * 1000), np.round(gyr * 10)]).astype(int)
    return from_raw(raw.flatten().tolist())


def synthetic_dataset(per_class: int, seed: int) -> tuple[list[Gesture], list[int]]:
    rng = np.random.default_rng(seed)
    gestures: list[Gesture] = []
    labels: list[int] = []
    for label in range(len(LABELS)):
        for _ in range(per_class):
            gestures.append(synthesize(label, rng))
            labels.append(label)
    return gestures, labels
