#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Run a zone experiment on nodes reached through TCP ports (IoT-LAB serial
ports through ssh tunnels, one local port per node).

The plan is a JSON list of nodes: {"node": name, "port": local port,
"role": "anchor" | "train" | "test", "zone": zone number}. The anchors stay
anchors. Each "train" node in turn becomes a tag and teaches its zone to the
locator, an anchor. Then the "test" nodes become tags, a few at a time, and
the locator gives their zone every second. The script prints the accuracy
and the confusion matrix, and writes all the serial lines to the log.

Only the Python standard library is needed.
"""

import argparse
import json
import re
import selectors
import socket
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ID_RE = re.compile(r"id ([0-9a-f]{4})")
ZONE_RE = re.compile(r"^Z ([0-9a-f]{4}) (\d+) (\d+) (\d+)")


@dataclass
class Node:
    name: str
    port: int
    role: str
    zone: int
    sock: socket.socket | None = None
    buf: bytes = b""
    lines: list[str] = field(default_factory=list)
    id: str = ""


class Testbed:
    def __init__(self, nodes: list[Node], log: Path) -> None:
        self.nodes = nodes
        self.sel = selectors.DefaultSelector()
        self.log = log.open("a")
        for n in nodes:
            n.sock = socket.create_connection(("127.0.0.1", n.port))
            n.sock.setblocking(False)
            self.sel.register(n.sock, selectors.EVENT_READ, n)

    def pump(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while (left := end - time.monotonic()) > 0:
            for key, _ in self.sel.select(timeout=min(left, 0.2)):
                n: Node = key.data
                try:
                    data = n.sock.recv(4096)
                except BlockingIOError:
                    continue
                if not data:
                    self.sel.unregister(n.sock)
                    continue
                n.buf += data
                while b"\n" in n.buf:
                    raw, n.buf = n.buf.split(b"\n", 1)
                    line = raw.decode(errors="replace").strip()
                    n.lines.append(line)
                    self.log.write(f"{time.time():.1f} {n.name} {line}\n")
        self.log.flush()

    def send(self, n: Node, command: str) -> None:
        """Send a command slowly, IoT-LAB serial links drop bytes now and then."""
        self.log.write(f"{time.time():.1f} {n.name} >>> {command}\n")
        for c in command + "\n":
            n.sock.setblocking(True)
            n.sock.send(c.encode())
            n.sock.setblocking(False)
            time.sleep(0.01)
        self.pump(0.1)

    def command(self, n: Node, command: str, expect: str, tries: int = 3) -> bool:
        """Send a command until its answer shows up."""
        for _ in range(tries):
            start = len(n.lines)
            self.send(n, command)
            self.pump(0.6)
            if any(expect in line for line in n.lines[start:]):
                return True
        print(f"{n.name}: no answer to '{command}'")
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plan", type=Path)
    parser.add_argument("--locator", required=True, help="name of the locator node")
    parser.add_argument("--teach", type=int, default=12, help="seconds per training tag")
    parser.add_argument("--test", type=int, default=10, help="seconds per test tag")
    parser.add_argument("--batch", type=int, default=3, help="test tags at a time")
    parser.add_argument("--log", type=Path, default=Path("zones.log"))
    args = parser.parse_args()

    plan = json.loads(args.plan.read_text())
    nodes = [Node(p["node"], p["port"], p["role"], p["zone"]) for p in plan]
    bed = Testbed(nodes, args.log)
    by_name = {n.name: n for n in nodes}
    loc = by_name[args.locator]

    print("reading the node ids")
    for n in nodes:
        if bed.command(n, "status", "id "):
            m = next((ID_RE.search(line) for line in reversed(n.lines) if ID_RE.search(line)),
                     None)
            n.id = m[1] if m else ""
    missing = [n.name for n in nodes if not n.id]
    if missing:
        print(f"no id for {missing}, they are left out")
    nodes = [n for n in nodes if n.id]

    print("roles: the tags stay quiet until their turn")
    for n in nodes:
        if n.role != "anchor":
            bed.command(n, "role off", "role off")
    bed.pump(3)
    bed.command(loc, "forget", "forgot")
    bed.pump(4)
    bed.command(loc, "status", "anchors")

    for n in [n for n in nodes if n.role == "train"]:
        bed.command(n, "role tag", "role tag")
        bed.pump(3)
        bed.command(loc, f"teach {n.id} {n.zone} {args.teach}", "teaching")
        bed.pump(args.teach + 2)
        bed.command(n, "role off", "role off")
        print(f"taught zone {n.zone} at {n.name}")
        bed.pump(1)
    bed.command(loc, "status", "fingerprints")

    results: dict[str, Counter[int]] = defaultdict(Counter)
    tests = [n for n in nodes if n.role == "test"]
    for i in range(0, len(tests), args.batch):
        batch = tests[i:i + args.batch]
        for n in batch:
            bed.command(n, "role tag", "role tag")
        bed.pump(3)
        start = len(loc.lines)
        bed.pump(args.test)
        ids = {n.id: n for n in batch}
        for line in loc.lines[start:]:
            m = ZONE_RE.match(line)
            if m and m[1] in ids:
                results[ids[m[1]].name][int(m[2])] += 1
        for n in batch:
            bed.command(n, "role off", "role off")
            got = results[n.name]
            print(f"{n.name} zone {n.zone}: located {dict(got)}")
        bed.pump(1)

    zones = sorted({n.zone for n in nodes})
    confusion = {z: Counter() for z in zones}
    right = total = 0
    for n in tests:
        for z, count in results[n.name].items():
            confusion[n.zone][z] += count
            total += count
            right += count if z == n.zone else 0
    majority = sum(1 for n in tests if results[n.name] and
                   results[n.name].most_common(1)[0][0] == n.zone)
    print(f"\n{right} of {total} seconds in the right zone "
          f"({100 * right / max(total, 1):.1f}%), {majority} of {len(tests)} test places "
          "right by majority")
    print("true zone: seconds located in zone " + " ".join(str(z) for z in zones))
    for z in zones:
        print(f"  {z}: " + " ".join(f"{confusion[z][p]:3d}" for p in zones))
    return 0


if __name__ == "__main__":
    sys.exit(main())
