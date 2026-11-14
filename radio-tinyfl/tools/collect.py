#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Live table of the TinyFL nodes.

Connects to the serial port of several nodes, IoT-LAB nodes through ssh
tunnels (one local TCP port per node), and shows the latest accuracies of
each node: its local model, its federated model and the nodes it knows.

Only the Python standard library is needed.
"""

import argparse
import re
import selectors
import socket
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

LINE_RE = re.compile(r"A (\w+) (\d+) local ([\d.]+) fed ([\d.]+) nodes (\d+) "
                     r"samples (\d+) (\d+) sent (\d+) received (\d+)")


@dataclass
class Node:
    port: int
    sock: socket.socket
    buf: bytes = b""
    last: list[str] = field(default_factory=list)


def show(nodes: list[Node]) -> None:
    out = ["\x1b[H\x1b[J", "port   node  time   local    fed  nodes  samples own/all"
           "   sent  received"]
    for n in nodes:
        if n.last:
            node, t, local, fed, known, own, total, sent, received = n.last
            bar = "#" * int(float(fed) / 5)
            out.append(f"{n.port:<6} {node:>4} {t:>5}s {local:>6}% {fed:>6}% {known:>5}  "
                       f"{own:>7}/{total:<7} {sent:>6} {received:>9}  {bar}")
        else:
            out.append(f"{n.port:<6} waiting")
    print("\n".join(out), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", type=int, nargs="+",
                        help="local TCP ports, one per node")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--log", type=Path, help="also write all the lines to this file")
    args = parser.parse_args()

    sel = selectors.DefaultSelector()
    nodes = []
    for port in args.ports:
        sock = socket.create_connection((args.host, port))
        sock.setblocking(False)
        node = Node(port, sock)
        nodes.append(node)
        sel.register(sock, selectors.EVENT_READ, node)

    log = args.log.open("a") if args.log else None
    try:
        last_show = 0.0
        while True:
            for key, _ in sel.select(timeout=1):
                node: Node = key.data
                data = node.sock.recv(4096)
                if not data:
                    sel.unregister(node.sock)
                    continue
                node.buf += data
                while b"\n" in node.buf:
                    raw, node.buf = node.buf.split(b"\n", 1)
                    line = raw.decode(errors="replace").strip()
                    if log:
                        log.write(f"{time.time():.1f} {node.port} {line}\n")
                    m = LINE_RE.search(line)
                    if m:
                        node.last = list(m.groups())
            if time.monotonic() - last_show > 1:
                show(nodes)
                last_show = time.monotonic()
    except KeyboardInterrupt:
        pass
    finally:
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
