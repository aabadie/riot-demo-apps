"""Run QR Printer on native, the printer on a pseudo terminal.

Sends texts with the qr shell command, decodes the bytes sent to the
printer and checks the printed QR codes: their layout and, when the
zxing-cpp Python package is installed, their content.
"""

import os
import pty
import select
import subprocess
import sys
import time
import tty
from pathlib import Path

from thermal_decode import WIDTH, decode

APP = Path(__file__).resolve().parents[2]
ELF = APP / "bin" / "native64" / "clue-qr-printer.elf"
QUIET = 4
DOT_PRINT_S = 0.030

try:
    import zxingcpp
    from PIL import Image
except ImportError:
    zxingcpp = None


class Board:
    """The application and the pseudo terminal of its printer."""

    def __init__(self) -> None:
        self.master, slave = pty.openpty()
        tty.setraw(self.master)
        tty.setraw(slave)
        self.proc = subprocess.Popen(
            [str(ELF), "-c", os.ttyname(slave)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.data = bytearray()
        self.times: list[float] = []
        self.console = ""
        self.wait_console("Ready", 5)

    def pump(self, timeout: float) -> None:
        out = self.proc.stdout.fileno()
        r, _, _ = select.select([self.master, out], [], [], timeout)
        now = time.monotonic()
        if self.master in r:
            chunk = os.read(self.master, 4096)
            self.data.extend(chunk)
            self.times.extend([now] * len(chunk))
        if out in r:
            self.console += os.read(out, 4096).decode(errors="replace")

    def wait_console(self, text: str, timeout: float) -> None:
        end = time.monotonic() + timeout
        while text not in self.console:
            if time.monotonic() > end:
                raise TimeoutError(f"no {text!r} in:\n{self.console}")
            self.pump(0.05)

    def send(self, line: bytes) -> None:
        self.proc.stdin.write(line + b"\n")
        self.proc.stdin.flush()

    def take(self) -> tuple[bytes, list[float]]:
        data, times = bytes(self.data), self.times
        self.data, self.times = bytearray(), []
        return data, times

    def close(self) -> None:
        self.proc.kill()
        self.proc.wait()


def modules(pixels: list[list[bool]]) -> list[list[bool]]:
    """The modules of a printed code: its side, centered, from its first row."""
    row = pixels[0]
    left = row.index(True)
    right = WIDTH - 1 - row[::-1].index(True)
    side = right - left + 1
    # the top left finder pattern is 7 modules wide
    scale = next(i for i in range(left, WIDTH) if not row[i]) - left
    assert scale % 7 == 0, f"finder of {scale} dots"
    scale //= 7
    size = side // scale
    assert size * scale == side and len(pixels) == side, "not square"
    assert (size - 17) % 4 == 0, f"size {size}"
    assert left == (WIDTH - side) // 2, "not centered"
    grid = [[pixels[y * scale + scale // 2][left + x * scale + scale // 2]
             for x in range(size)] for y in range(size)]
    # each module is a full square of dots
    for y in range(side):
        for x in range(side):
            assert pixels[y][left + x] == grid[y // scale][x // scale]
    return grid, scale


def read_code(grid: list[list[bool]]) -> str | None:
    if zxingcpp is None:
        return None
    size = len(grid) + 2 * QUIET
    img = Image.new("L", (size, size), 255)
    for y, row in enumerate(grid):
        for x, black in enumerate(row):
            if black:
                img.putpixel((x + QUIET, y + QUIET), 0)
    img = img.resize((size * 4, size * 4), Image.NEAREST)
    found = zxingcpp.read_barcodes(img, formats=zxingcpp.BarcodeFormat.QRCode)
    return found[0].text if found else ""


def check_print(board: Board, text: str, arg: bytes | None = None) -> None:
    board.console = ""
    board.send(b"qr " + (arg if arg is not None else text.encode()))
    board.wait_console("Printed", 60)
    time.sleep(0.5)
    board.pump(0.1)
    out = decode(*board.take())
    assert len(out.bitmaps) == 1, f"{len(out.bitmaps)} images"
    bitmap = out.bitmaps[0]
    grid, scale = modules(bitmap.pixels())
    assert 4 <= scale <= 8, f"scale {scale}"
    feeds = [c[1] for c in out.commands if c[0] == "J"]
    assert feeds == [QUIET * scale] * 2, f"quiet zone feeds {feeds}"
    for (t0, rows), (t1, _) in zip(bitmap.chunks, bitmap.chunks[1:]):
        assert t1 - t0 >= rows * DOT_PRINT_S * 0.95, "printer overrun"
    caption = text if len(text) <= 96 and text.isascii() and text.isprintable() \
        else f"({len(text.encode())} bytes)"
    assert caption in out.text, f"caption {out.text!r}"
    read = read_code(grid)
    status = "not decoded, no zxing-cpp" if read is None else "decoded"
    if read is not None:
        assert read == text, f"read {read!r}"
    print(f"ok: {len(text.encode())} bytes, version {(len(grid) - 17) // 4}, "
          f"{scale} dots per module, {status}")


def main() -> int:
    board = Board()
    try:
        check_print(board, "Hello RIOT")
        check_print(board, "https://www.riot-os.org/")
        check_print(board, "two  spaces", b"'two  spaces'")
        check_print(board, "café à Paris")
        long_text = ("RIOT, the friendly operating system for the IoT. " * 11)[:512]
        check_print(board, long_text)

        board.console = ""
        board.send(b"qr " + b"x" * 513)
        board.wait_console("text too long", 5)
        print("ok: 513 bytes refused")

        board.console = ""
        board.send(b"qr first")
        board.wait_console("queued", 5)
        board.send(b"qr second")
        board.wait_console("busy", 5)
        board.wait_console("Printed", 60)
        print("ok: busy while printing")
    finally:
        board.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
