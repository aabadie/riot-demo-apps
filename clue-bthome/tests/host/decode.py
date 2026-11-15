#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Inria
# SPDX-License-Identifier: LGPL-2.1-only
"""Decode the advertisements printed by test_bthome with bthome-ble, the
parser of Home Assistant, and check the values."""

import sys

import time

from bluetooth_sensor_state_data import SensorUpdate
from bthome_ble import BTHomeBluetoothDeviceData
from habluetooth.models import BluetoothServiceInfoBleak

UUID = "0000fcd2-0000-1000-8000-00805f9b34fb"

EXPECTED: dict[str, dict[str, object]] = {
    "full": {"temperature": 21.37, "pressure": 1013.25, "illuminance": 312.0,
             "humidity": 45, "moving": True, "vibration": False},
    "events": {"temperature": -5.5, "moving": False, "vibration": True,
               "button_1": "double_press", "button_2": "long_press"},
    "few": {"temperature": 30.0},
}

# the stream of test_bthome: each event once, the last values sent
STREAM_EVENTS: list[tuple[str, str]] = [("button_1", "press"), ("button_2", "double_press"),
                                        ("button_1", "long_press")]
STREAM_LAST: dict[str, object] = {"temperature": 21.5, "moving": True}
REPEATS = 3


def _info(data: bytes, when: float) -> BluetoothServiceInfoBleak:
    # the advertised service data starts after the 16-bit UUID
    return BluetoothServiceInfoBleak(name="CLUE-TEST", address="AA:BB:CC:DD:EE:FF", rssi=-60,
                                     manufacturer_data={}, service_data={UUID: data[2:]},
                                     service_uuids=[UUID], source="local", device=None,
                                     advertisement=None, connectable=False, time=when,
                                     tx_power=None)


def _values(update: SensorUpdate) -> dict[str, object]:
    values: dict[str, object] = {}
    for key, value in update.entity_values.items():
        values[key.key] = value.native_value
    for key, value in update.binary_entity_values.items():
        values[key.key] = value.native_value
    return values


def decode(name: str, data: bytes) -> dict[str, object]:
    parser = BTHomeBluetoothDeviceData()
    update: SensorUpdate = parser.update(_info(data, time.monotonic()))
    values = _values(update)
    for key, event in update.events.items():
        values[key.key] = event.event_type
    print(f"{name}: {values}")
    return values


def _check_stream(packets: list[bytes]) -> bool:
    """Feed the stream to one parser, each packet heard REPEATS times like the
    advertising repeats it, and check that each event comes out once."""
    parser = BTHomeBluetoothDeviceData()
    events: list[tuple[str, str]] = []
    last: dict[str, object] = {}
    when = time.monotonic()
    for data in packets:
        for _ in range(REPEATS):
            update: SensorUpdate = parser.update(_info(data, when))
            when += 0.1
            events += [(key.key, event.event_type) for key, event in update.events.items()]
            last.update(_values(update))
    ok = events == STREAM_EVENTS and all(
        abs(last.get(k) - v) < 0.006 if isinstance(v, float) else last.get(k) == v
        for k, v in STREAM_LAST.items())
    print(f"stream: {len(packets)} packets, events {events}, last {last}")
    if not ok:
        print(f"  expected events {STREAM_EVENTS}, last {STREAM_LAST}")
    return ok


def main() -> int:
    failed = False
    seen = set()
    stream: list[bytes] = []
    for line in sys.stdin:
        if line.startswith("#"):
            print(line.strip())
            failed |= "FAIL" in line
            continue
        if line.startswith("stream "):
            stream.append(bytes.fromhex(line.split()[1]))
            continue
        name, hex_data = line.split()
        values = decode(name, bytes.fromhex(hex_data))
        seen.add(name)
        for key, want in EXPECTED[name].items():
            got = values.get(key)
            ok = abs(got - want) < 0.006 if isinstance(want, float) and \
                isinstance(got, float) else got == want
            if not ok:
                print(f"  {key}: got {got!r}, expected {want!r}")
                failed = True
    failed |= not _check_stream(stream)
    if seen != set(EXPECTED):
        print(f"missing advertisements: {set(EXPECTED) - seen}")
        failed = True
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
