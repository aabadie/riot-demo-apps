#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Run TinyFL on several native nodes connected by the ZEP dispatcher of
RIOT, and check the results of each node at the end.

With the flat topology every node hears every other one: each node must
know all the others, its local model must be right about half of the time
and its federated model much more often. With the line topology the nodes
form a chain: the statistics must be relayed, up to 4 hops. With
--restart, the first node is restarted during the run: at the end, once all
the nodes have all their samples, every node must have the new statistics
of the restarted node, not the ones from before.
"""

import argparse
import re
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

APP_DIR: Path = Path(__file__).resolve().parent.parent.parent
ELF: Path = APP_DIR / "bin" / "native64" / "radio-tinyfl.elf"
LINE_RE = re.compile(r"A (\w+) (\d+) local ([\d.]+) fed ([\d.]+) nodes (\d+) "
                     r"samples (\d+) (\d+)")
MAX_HOPS: int = 4


@dataclass
class Result:
    node: str
    seconds: int
    local: float
    fed: float
    nodes: int
    own: int
    total: int


def parse(log: Path) -> Result | None:
    last = None
    for line in log.read_text(errors="replace").splitlines():
        m = LINE_RE.search(line)
        if m:
            last = Result(m[1], int(m[2]), float(m[3]), float(m[4]), int(m[5]), int(m[6]),
                          int(m[7]))
    return last


def expected_nodes(index: int, count: int, line: bool) -> set[int]:
    """Number of nodes known by node at index (itself included)."""
    if not line:
        return {count}
    # on a chain, the order of the nodes is the order they joined the
    # dispatcher, unknown here: accept what a node anywhere could know
    return {min(count, i + MAX_HOPS + 1) - max(0, i - MAX_HOPS) for i in range(count)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nodes", type=int, default=4)
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--topology", choices=["flat", "line"], default="flat")
    parser.add_argument("--dispatcher", type=Path, required=True,
                        help="zep_dispatch binary of RIOT")
    parser.add_argument("--port", type=int, default=17760)
    parser.add_argument("--min-fed", type=float, default=80.0)
    parser.add_argument("--max-local", type=float, default=55.0)
    parser.add_argument("--restart", type=int, metavar="SECONDS",
                        help="restart the first node after this time")
    args = parser.parse_args()

    procs: list[subprocess.Popen[bytes]] = []
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp = Path(tmp_dir)
        cmd = [str(args.dispatcher)]
        if args.topology == "line":
            names = [chr(ord("A") + i) for i in range(args.nodes)]
            topo = tmp / "line.topo"
            topo.write_text("".join(f"{a}\t{b}\n" for a, b in zip(names, names[1:])))
            cmd += ["-t", str(topo)]
        cmd += ["::1", str(args.port)]

        def start(i: int, log: Path) -> subprocess.Popen[bytes]:
            eui = f"02:00:00:00:00:00:{0x10 + i:02x}:{0x31 * (i + 1) & 0xff:02x}"
            with log.open("wb") as out:
                return subprocess.Popen(
                    [str(ELF), "-z", f"[::1]:{args.port}", "-U", eui],
                    stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

        logs = []
        try:
            procs.append(subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                          stderr=subprocess.DEVNULL,
                                          stdin=subprocess.DEVNULL))
            time.sleep(0.5)
            if procs[0].poll() is not None:
                print(f"the dispatcher did not start, is port {args.port} free?")
                return 1
            for i in range(args.nodes):
                logs.append(tmp / f"node{i}.log")
                procs.append(start(i, logs[i]))
                time.sleep(0.2)
            if args.restart:
                time.sleep(args.restart)
                procs[1].send_signal(signal.SIGTERM)
                procs[1].wait(timeout=5)
                print(f"node 0 restarted after {args.restart} s")
                logs[0] = tmp / "node0-restarted.log"
                procs[1] = start(0, logs[0])
                time.sleep(args.seconds - args.restart)
            else:
                time.sleep(args.seconds)
        finally:
            for p in reversed(procs):
                p.send_signal(signal.SIGTERM)
            for p in procs:
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()

        failed = False
        allowed = expected_nodes(0, args.nodes, args.topology == "line")
        seen = []
        results = []
        for i, log in enumerate(logs):
            r = parse(log)
            if r is None:
                print(f"node {i}: no result")
                print(log.read_text(errors="replace")[-2000:])
                failed = True
                continue
            seen.append(r.nodes)
            results.append(r)
            print(f"node {r.node} after {r.seconds} s: local {r.local}%, federated "
                  f"{r.fed}%, knows {r.nodes} nodes, {r.own} own samples of {r.total}")
            if r.local > args.max_local or r.fed < args.min_fed or r.nodes not in allowed:
                failed = True
        if args.topology == "line" and args.nodes > 2 * MAX_HOPS + 1 and \
                max(seen, default=0) == args.nodes:
            print("a node of the line knows all the others, relaying is not limited")
            failed = True

        if args.restart and len(results) == args.nodes:
            total = sum(r.own for r in results)
            stale = [r.node for r in results if r.total != total]
            if stale:
                print(f"nodes without the {total} samples of the federation: {stale}")
                failed = True
            if "jumping past it" not in logs[0].read_text(errors="replace"):
                print("the restarted node did not see its old sequence number")
                failed = True

    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
