"""Decode the bytes sent to an Adafruit thermal printer.

The commands of the adafruit_thermal driver of RIOT, a subset of ESC/POS.
"""

from dataclasses import dataclass, field

ESC = 0x1B
GS = 0x1D
DC2 = 0x12
WIDTH = 384


class DecodeError(Exception):
    """Bytes that are not a known command."""


@dataclass
class Bitmap:
    """An image, made of the chunks of DC2 * commands."""

    row_len: int
    rows: list[bytes] = field(default_factory=list)
    # arrival time of the first byte of each chunk, and its rows
    chunks: list[tuple[float, int]] = field(default_factory=list)

    def pixels(self) -> list[list[bool]]:
        """Rows of dots, True for black."""
        out: list[list[bool]] = []
        for row in self.rows:
            dots = [bool(b & (0x80 >> i)) for b in row for i in range(8)]
            out.append(dots + [False] * (WIDTH - len(dots)))
        return out


@dataclass
class Printout:
    """What the printer received."""

    commands: list[tuple] = field(default_factory=list)
    text: str = ""
    bitmaps: list[Bitmap] = field(default_factory=list)


def decode(data: bytes, times: list[float] | None = None) -> Printout:
    """Decode a byte stream, times[i] is the arrival time of data[i]."""
    if times is None:
        times = [0.0] * len(data)
    out = Printout()
    bitmap: Bitmap | None = None
    i = 0

    def take(n: int) -> bytes:
        nonlocal i
        if i + n > len(data):
            raise DecodeError(f"truncated command at {i}")
        chunk = data[i:i + n]
        i += n
        return chunk

    while i < len(data):
        start = i
        b = take(1)[0]
        if b == DC2 and data[i:i + 1] == b"*":
            take(1)
            rows, row_len = take(2)
            if bitmap is None or bitmap.row_len != row_len:
                bitmap = Bitmap(row_len)
                out.bitmaps.append(bitmap)
            bitmap.chunks.append((times[start], rows))
            for _ in range(rows):
                bitmap.rows.append(take(row_len))
            continue
        bitmap = None
        if b == 0xFF:
            out.commands.append(("wake",))
        elif b == ESC:
            c = take(1)
            if c == b"@":
                out.commands.append(("reset",))
            elif c == b"D":
                tabs = []
                while (t := take(1)[0]) != 0:
                    tabs.append(t)
                out.commands.append(("tabs", tabs))
            elif c in (b"8",):
                lo, hi = take(2)
                out.commands.append(("sleep", lo | hi << 8))
            elif c == b"7":
                out.commands.append(("heat", *take(3)))
            elif c in (b"!", b"a", b"d", b"J", b"E", b"-"):
                out.commands.append((c.decode(), take(1)[0]))
                if c == b"d":
                    out.text += "\n" * data[i - 1]
            else:
                raise DecodeError(f"unknown ESC {c!r} at {start}")
        elif b == GS:
            c = take(1)
            if c in (b"a", b"!", b"B"):
                out.commands.append(("GS " + c.decode(), take(1)[0]))
            else:
                raise DecodeError(f"unknown GS {c!r} at {start}")
        elif b == DC2:
            c = take(1)
            if c == b"#":
                out.commands.append(("density", take(1)[0]))
            elif c == b"T":
                out.commands.append(("testpage",))
            else:
                raise DecodeError(f"unknown DC2 {c!r} at {start}")
        elif b == 0x0A or 0x20 <= b < 0x7F:
            out.text += chr(b)
        else:
            raise DecodeError(f"unexpected byte 0x{b:02x} at {start}")
    return out
