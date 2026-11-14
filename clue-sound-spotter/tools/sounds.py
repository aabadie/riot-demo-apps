# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Sounds of the Sound Spotter: labels, recordings, synthetic sounds, and
the C code of the board called through ctypes."""

import ctypes
import math
import subprocess
import tempfile
import wave
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

LABELS: list[str] = ["CLAP", "SNAP", "WHISTLE", "KNOCK"]
RATE: int = 16000
FRAME: int = 256
MAX_FRAMES: int = 64
FEATURES: int = 2 * 16 + 8
APP_DIR: Path = Path(__file__).resolve().parent.parent

Pcm = NDArray[np.int16]


def emlearn_include() -> str:
    saved = np.geterr()
    import emlearn
    # emlearn switches numpy to raise on any floating point error
    np.seterr(**saved)
    return emlearn.includedir


class SoundLib:
    """sound.c and sound_eml.c, with model.h or not, as a shared library."""

    def __init__(self, with_model: bool, build_dir: Path,
                 model_dir: Path = APP_DIR) -> None:
        sources = [APP_DIR / "sound.c", APP_DIR / "sound_eml.c"]
        defines = ["-DSOUND_HAVE_MODEL"] if with_model else []
        lib = build_dir / ("libsound_model.so" if with_model else "libsound.so")
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-Wall", "-Werror",
                        *defines, f"-I{model_dir}", f"-I{APP_DIR}",
                        f"-I{emlearn_include()}", *map(str, sources),
                        "-o", str(lib), "-lm"], check=True)
        self.lib = ctypes.CDLL(str(lib))
        self.lib.sound_init()

    @staticmethod
    def _ptr(array: NDArray, ctype: type) -> ctypes._Pointer:
        return array.ctypes.data_as(ctypes.POINTER(ctype))

    def find_event(self, pcm: Pcm) -> Pcm | None:
        """The samples of the first event, as the board would cut it."""
        n_frames = len(pcm) // FRAME
        pcm = np.ascontiguousarray(pcm[:n_frames * FRAME], dtype=np.int16)
        first = ctypes.c_uint(0)
        n = self.lib.sound_find_event(self._ptr(pcm, ctypes.c_int16),
                                      ctypes.c_uint(n_frames),
                                      ctypes.byref(first))
        if n == 0:
            return None
        return pcm[first.value * FRAME:(first.value + n) * FRAME]

    def features(self, event: Pcm) -> NDArray[np.int16] | None:
        event = np.ascontiguousarray(event, dtype=np.int16)
        out = np.zeros(FEATURES, dtype=np.int16)
        ret = self.lib.sound_features_pcm(self._ptr(event, ctypes.c_int16),
                                          ctypes.c_uint(len(event) // FRAME),
                                          self._ptr(out, ctypes.c_int16))
        return out if ret == 0 else None

    def classify(self, features: NDArray[np.int16]) -> int:
        feats = np.ascontiguousarray(features, dtype=np.int16)
        proba = np.zeros(len(LABELS), dtype=np.float32)
        return self.lib.sound_classify(self._ptr(feats, ctypes.c_int16),
                                       self._ptr(proba, ctypes.c_float))


def build_dir() -> tempfile.TemporaryDirectory:
    return tempfile.TemporaryDirectory(prefix="sound-spotter-")


def save_wav(path: Path, pcm: Pcm) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.astype("<i2").tobytes())


def load_wav(path: Path) -> Pcm:
    with wave.open(str(path), "rb") as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").copy()


def load_dataset(directory: Path) -> tuple[list[Pcm], list[int]]:
    """Events recorded by record.py: <directory>/<LABEL>/<n>.wav, already cut
    by the board."""
    events: list[Pcm] = []
    labels: list[int] = []
    for index, label in enumerate(LABELS):
        for path in sorted((directory / label).glob("*.wav")):
            events.append(load_wav(path))
            labels.append(index)
    return events, labels


def _noise(rng: np.random.Generator, n: int, level: float) -> NDArray[np.float64]:
    """Background noise falling with the frequency, like in a room."""
    spec = np.fft.rfft(rng.normal(0, 1, n))
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    spec /= np.sqrt(1 + freqs / 200.0)
    out = np.fft.irfft(spec, n)
    return out / (np.std(out) + 1e-9) * level


def _burst(rng: np.random.Generator, n: int, tau: float, lo: float,
           hi: float) -> NDArray[np.float64]:
    """Band limited noise burst with an exponential decay."""
    spec = np.fft.rfft(rng.normal(0, 1, n))
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    spec[(freqs < lo) | (freqs > hi)] *= 0.05
    noise = np.fft.irfft(spec, n)
    t = np.arange(n) / RATE
    return noise / (np.std(noise) + 1e-9) * np.exp(-t / tau)


def synthesize(label: int, rng: np.random.Generator) -> Pcm:
    """A rough model of each sound in a quiet room, for a default model and
    the tests. Recordings of the real sounds make a much better model."""
    name = LABELS[label]
    total = int(1.6 * RATE)
    sig = np.zeros(total)
    start = int(rng.uniform(0.3, 0.4) * RATE)
    if name == "CLAP":
        n = int(0.35 * RATE)
        s = _burst(rng, n, rng.uniform(0.012, 0.035), 500, 6000)
        s += 0.1 * _burst(rng, n, 0.08, 300, 4000)    # room
        sig[start:start + n] += s
    elif name == "SNAP":
        n = int(0.15 * RATE)
        s = _burst(rng, n, rng.uniform(0.004, 0.010), 1500, 7500)
        sig[start:start + n] += s
    elif name == "WHISTLE":
        dur = rng.uniform(0.3, 0.9)
        n = int(dur * RATE)
        t = np.arange(n) / RATE
        f0 = rng.uniform(1000, 2800)
        f = f0 * (1 + 0.02 * np.sin(2 * math.pi * rng.uniform(4, 6) * t))
        phase = 2 * math.pi * np.cumsum(f) / RATE
        env = np.minimum(1, np.minimum(t, dur - t) / 0.04)
        s = (np.sin(phase) + 0.1 * np.sin(2 * phase)) * env
        s += 0.05 * rng.normal(0, 1, n) * env              # breath
        sig[start:start + n] += s
    elif name == "KNOCK":
        knocks = int(rng.integers(2, 4))
        gap = rng.uniform(0.12, 0.2)
        n = int(0.12 * RATE)
        t = np.arange(n) / RATE
        for k in range(knocks):
            f = rng.uniform(100, 300)
            tau = rng.uniform(0.02, 0.05)
            s = np.sin(2 * math.pi * f * t) * np.exp(-t / tau)
            s += 0.4 * np.sin(2 * math.pi * 2.7 * f * t) * np.exp(-t / (tau / 2))
            s += 0.3 * _burst(rng, n, 0.003, 500, 4000)  # click
            pos = start + int(k * gap * RATE)
            sig[pos:pos + n] += s
    amp = rng.uniform(0.05, 0.6)
    sig = sig / (np.max(np.abs(sig)) + 1e-9) * amp
    sig += _noise(rng, total, 10 ** (rng.uniform(-62, -48) / 20))
    return np.clip(sig * 32767, -32768, 32767).astype(np.int16)


def synthetic_dataset(per_class: int, seed: int) -> tuple[list[Pcm], list[int]]:
    rng = np.random.default_rng(seed)
    clips: list[Pcm] = []
    labels: list[int] = []
    for label in range(len(LABELS)):
        for _ in range(per_class):
            clips.append(synthesize(label, rng))
            labels.append(label)
    return clips, labels
