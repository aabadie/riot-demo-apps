#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Live view of the Jam Detector scans.

Reads the serial output of a node, from a TCP port (the serial port of an
IoT-LAB node through an ssh tunnel) or from a local serial port, and shows
one colored row per scan: the occupancy of channels 11 to 26, the detector
level and the alarms. Commands typed after --send are sent first, for
example to start a test transmitter on another node.

Only the Python standard library is needed, pyserial for a local port.
"""

import argparse
import socket
import sys
import time
from collections.abc import Iterator
from pathlib import Path
from typing import TextIO

SHADES: str = " .:-=+*#%@"
CHANNELS: list[int] = list(range(11, 27))


def tcp_lines(host: str, port: int, send: list[str]) -> Iterator[str]:
    with socket.create_connection((host, port)) as sock:
        for command in send:
            # the IoT-LAB serial link drops characters when sent too fast
            for ch in command + "\n":
                sock.sendall(ch.encode())
                time.sleep(0.02)
        buf = b""
        while True:
            data = sock.recv(4096)
            if not data:
                return
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                yield line.decode(errors="replace").strip()


def serial_lines(port: str, send: list[str]) -> Iterator[str]:
    import serial
    with serial.Serial(port, 115200, timeout=1) as ser:
        for command in send:
            ser.write((command + "\n").encode())
        while True:
            line = ser.readline()
            if line:
                yield line.decode(errors="replace").strip()


def color(level: int) -> str:
    """ANSI 256 color from blue (clear) to red (busy), level 0 to 15."""
    palette = [17, 18, 19, 20, 25, 31, 37, 43, 79, 115, 186, 221, 215, 209, 203, 196]
    return f"\x1b[48;5;{palette[level]}m"


def show_scan(fields: list[str], out: TextIO) -> None:
    _, scan, state, level, occupancy, frames, crc = fields
    cells = "".join(color(int(c, 16)) + SHADES[int(c, 16) * 9 // 15] * 2 for c in occupancy)
    tag = {"L": "learn", "D": "     ", "A": "ALARM"}.get(state, state)
    bar = "#" * min(int(level) // 5, 30)
    out.write(f"{int(scan):6d} {tag} {cells}\x1b[0m {int(level) / 10:5.1f} {bar:<30} "
              f"rx {frames:>3} crc {crc:>3}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--tcp", metavar="HOST:PORT",
                        help="serial port of an IoT-LAB node through an ssh tunnel")
    source.add_argument("--port", help="local serial port, /dev/ttyACM0 for example")
    parser.add_argument("--send", action="append", default=[], metavar="COMMAND",
                        help="shell command to send first, can be repeated")
    parser.add_argument("--log", type=Path, help="also write the raw lines to this file")
    args = parser.parse_args()

    if args.tcp:
        host, port = args.tcp.rsplit(":", 1)
        lines = tcp_lines(host, int(port), args.send)
    else:
        lines = serial_lines(args.port, args.send)

    log = args.log.open("a") if args.log else None
    header = "".join(f"{c:<2}" for c in CHANNELS)
    print(f"  scan       {header}  level")
    try:
        for line in lines:
            if log:
                log.write(line + "\n")
                log.flush()
            fields = line.split()
            if len(fields) == 7 and fields[0] == "S":
                show_scan(fields, sys.stdout)
            elif line:
                print(f"\x1b[1m{line}\x1b[0m")
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
