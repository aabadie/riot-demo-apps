#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Send MNIST test digits to the Digit Oracle and show what it recognizes.

Each digit is picked in the MNIST test set, shown in the terminal (or in a
matplotlib window with --plot), sent to the board over its USB serial port,
and the answer of the board is printed next to the expected digit.

With --exec, the digits are sent to a program speaking the same protocol
on its stdin and stdout instead, such as tests/host/digit_host.

The listen command shows the digits drawn on the board instead.
"""

import argparse
import random
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Protocol

import numpy as np
from numpy.typing import NDArray

from mnist import SIZE, load_mnist

SHADES: str = " .:-=+*#%@"
TEST_START: int = 60000


class Link(Protocol):
    def send(self, line: str) -> None: ...
    def receive(self, timeout: float) -> str | None: ...
    def close(self) -> None: ...


class SerialLink:
    """The board, over its USB serial port."""

    def __init__(self, port: str, baudrate: int) -> None:
        import serial

        self.serial = serial.Serial(port, baudrate, timeout=0.1)
        self.buffer = b""

    def send(self, line: str) -> None:
        self.serial.write(line.encode("ascii") + b"\n")
        self.serial.flush()

    def receive(self, timeout: float) -> str | None:
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            if time.monotonic() > deadline:
                return None
            self.buffer += self.serial.read(self.serial.in_waiting or 1)
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode("ascii", "replace").strip()

    def close(self) -> None:
        self.serial.close()


class ProcessLink:
    """A host program speaking the protocol on its stdin and stdout."""

    def __init__(self, command: str) -> None:
        self.process = subprocess.Popen(command, shell=True, text=True,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE)

    def send(self, line: str) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def receive(self, timeout: float) -> str | None:
        assert self.process.stdout is not None
        line = self.process.stdout.readline()
        return line.strip() if line else None

    def close(self) -> None:
        assert self.process.stdin is not None
        self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            # RIOT native keeps running at the end of its stdin
            self.process.kill()
            self.process.wait()


@dataclass
class Result:
    digit: int
    us: int
    proba: list[int]


def connect(link: Link) -> None:
    """Wait for the banner, the board may still be printing its boot log."""
    for _ in range(20):
        link.send("?")
        deadline = time.monotonic() + 0.5
        while time.monotonic() < deadline:
            line = link.receive(0.5)
            if line is None:
                break
            if line.startswith("I digit-oracle"):
                return
    sys.exit("no answer from the Digit Oracle, is it flashed and running?")


def classify(link: Link, image: NDArray[np.uint8], label: int,
             normalize: bool) -> Result:
    cmd = "N" if normalize else "D"
    link.send(f"{cmd} {label} {image.tobytes().hex()}")
    while True:
        line = link.receive(3.0)
        if line is None:
            sys.exit("no answer from the board")
        if line.startswith("E "):
            sys.exit(f"the board says: {line[2:]}")
        if line.startswith("P "):
            fields = [int(f) for f in line.split()[1:]]
            return Result(fields[0], fields[1], fields[2:])


def distort(image: NDArray[np.uint8], rng: random.Random) -> NDArray[np.uint8]:
    """Shrink and move the digit, for the board to normalize it back."""
    scale = rng.uniform(0.5, 0.9)
    size = max(4, int(SIZE * scale))
    idx = (np.arange(size) * SIZE / size).astype(int)
    small = image[np.ix_(idx, idx)]
    out = np.zeros_like(image)
    x = rng.randint(0, SIZE - size)
    y = rng.randint(0, SIZE - size)
    out[y:y + size, x:x + size] = small
    return out


def ascii_art(image: NDArray[np.uint8]) -> list[str]:
    rows = []
    for row in image:
        rows.append("".join(SHADES[int(v) * (len(SHADES) - 1) // 255] * 2
                            for v in row))
    return rows


def show_text(image: NDArray[np.uint8], label: int, result: Result) -> None:
    mark = "ok" if result.digit == label else "WRONG"
    bars = [f"{d}: {'#' * (p // 4):<25} {p:3d}%"
            for d, p in enumerate(result.proba)]
    art = ascii_art(image)
    pad = (len(art) - len(bars)) // 2
    for i, row in enumerate(art):
        j = i - pad
        side = bars[j] if 0 <= j < len(bars) else ""
        print(f"{row}  {side}")
    print(f"expected {label}, board says {result.digit} "
          f"({result.proba[result.digit]}%, {result.us} us): {mark}\n")


class Plot:
    """Image and probabilities in a matplotlib window."""

    def __init__(self) -> None:
        import matplotlib.pyplot as plt

        self.plt = plt
        plt.ion()
        self.fig, (self.ax_img, self.ax_bar) = plt.subplots(
            1, 2, figsize=(8, 4))

    def show(self, image: NDArray[np.uint8], label: int,
             result: Result | None) -> None:
        self.ax_img.clear()
        self.ax_img.imshow(image, cmap="gray_r", vmin=0, vmax=255)
        self.ax_img.set_axis_off()
        self.ax_bar.clear()
        self.ax_bar.set_ylim(0, 100)
        self.ax_bar.set_xticks(range(10))
        if result is None:
            self.ax_img.set_title(f"sending a {label}...")
        else:
            colors = ["tab:green" if d == label else "tab:red"
                      if d == result.digit else "tab:gray" for d in range(10)]
            self.ax_bar.bar(range(10), result.proba, color=colors)
            self.ax_img.set_title(f"expected {label}, board says "
                                  f"{result.digit}")
            self.ax_bar.set_title(f"probabilities (%), {result.us} us")
        self.fig.canvas.draw_idle()
        self.plt.pause(0.05)


def open_link(args: argparse.Namespace) -> Link:
    if args.exec:
        return ProcessLink(args.exec)
    return SerialLink(args.port, args.baudrate)


def cmd_send(args: argparse.Namespace) -> int:
    images, labels = load_mnist()
    rng = random.Random(args.seed)
    if args.index is not None:
        indices = [TEST_START + args.index]
    else:
        pool = range(TEST_START, len(labels))
        if args.digit is not None:
            pool = [i for i in pool if labels[i] == args.digit]
        indices = rng.sample(list(pool), min(args.count, len(pool)))

    plot = Plot() if args.plot else None
    link = open_link(args)
    connect(link)

    correct = 0
    times: list[int] = []
    wrong: dict[tuple[int, int], int] = {}
    for n, i in enumerate(indices):
        image = images[i]
        if args.normalize:
            image = distort(image, rng)
        label = int(labels[i])
        if plot:
            plot.show(image, label, None)
        result = classify(link, image, label, args.normalize)
        times.append(result.us)
        if result.digit == label:
            correct += 1
        else:
            wrong[(label, result.digit)] = wrong.get((label, result.digit), 0) + 1
        if plot:
            plot.show(image, label, result)
        if not args.quiet:
            print(f"[{n + 1}/{len(indices)}] MNIST test digit {i - TEST_START}")
            show_text(image, label, result)
        if args.delay and n + 1 < len(indices):
            time.sleep(args.delay)
    link.close()

    accuracy = correct / len(indices)
    print(f"{correct}/{len(indices)} correct ({accuracy:.2%}), "
          f"inference {int(np.mean(times))} us on average")
    if wrong and len(indices) > 1:
        worst = sorted(wrong.items(), key=lambda kv: -kv[1])[:5]
        print("most frequent mistakes: " +
              ", ".join(f"{a} read as {b} ({c}x)" for (a, b), c in worst))
    if plot:
        plot.plt.ioff()
        plot.plt.show()
    return 0 if accuracy >= args.min_accuracy else 1


def cmd_listen(args: argparse.Namespace) -> int:
    """Show the digits drawn on the board and sent with the submit button."""
    plot = Plot() if args.plot else None
    link = open_link(args)
    print("waiting for digits drawn on the board, Ctrl-C to stop")
    image: NDArray[np.uint8] | None = None
    try:
        while True:
            line = link.receive(1.0)
            if not line:
                continue
            if line.startswith("B "):
                data = bytes.fromhex(line[2:])
                image = np.frombuffer(data, dtype=np.uint8).reshape(SIZE, SIZE)
            elif line.startswith("P ") and image is not None:
                fields = [int(f) for f in line.split()[1:]]
                result = Result(fields[0], fields[1], fields[2:])
                if plot:
                    plot.show(image, result.digit, result)
                for row in ascii_art(image):
                    print(row)
                print(f"board says {result.digit} "
                      f"({result.proba[result.digit]}%)\n")
                image = None
    except KeyboardInterrupt:
        pass
    link.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="serial port of the board (default /dev/ttyACM0)")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--exec", metavar="COMMAND",
                        help="talk to this program instead of the board")
    parser.add_argument("--plot", action="store_true",
                        help="show the digits in a matplotlib window")
    sub = parser.add_subparsers(dest="command")

    send = sub.add_parser("send", help="send MNIST digits (default)")
    listen = sub.add_parser("listen", help="show the digits drawn on the board")
    listen.set_defaults(func=cmd_listen)
    for p in (parser, send):
        p.add_argument("--count", type=int, default=1,
                       help="number of digits to send (default 1)")
        p.add_argument("--digit", type=int, choices=range(10),
                       help="only send this digit")
        p.add_argument("--index", type=int,
                       help="send this digit of the test set (0 to 9999)")
        p.add_argument("--seed", type=int, help="random seed")
        p.add_argument("--delay", type=float, default=0.0,
                       help="seconds between two digits")
        p.add_argument("--normalize", action="store_true",
                       help="shrink and move the digits, the board "
                            "normalizes them back")
        p.add_argument("--quiet", action="store_true",
                       help="only print the summary")
        p.add_argument("--min-accuracy", type=float, default=0.0,
                       help="exit with an error below this accuracy")
    send.set_defaults(func=cmd_send)
    parser.set_defaults(func=cmd_send)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
