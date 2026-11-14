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


def decode(name: str, data: bytes) -> dict[str, object]:
    parser = BTHomeBluetoothDeviceData()
    # the advertised service data starts after the 16-bit UUID
    info = BluetoothServiceInfoBleak(name="CLUE-TEST", address="AA:BB:CC:DD:EE:FF", rssi=-60,
                                     manufacturer_data={}, service_data={UUID: data[2:]},
                                     service_uuids=[UUID], source="local", device=None,
                                     advertisement=None, connectable=False,
                                     time=time.monotonic(), tx_power=None)
    update: SensorUpdate = parser.update(info)
    values: dict[str, object] = {}
    for key, value in update.entity_values.items():
        values[key.key] = value.native_value
    for key, value in update.binary_entity_values.items():
        values[key.key] = value.native_value
    for key, event in update.events.items():
        values[key.key] = event.event_type
    print(f"{name}: {values}")
    return values


def main() -> int:
    failed = False
    seen = set()
    for line in sys.stdin:
        if line.startswith("#"):
            print(line.strip())
            failed |= "FAIL" in line
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
    if seen != set(EXPECTED):
        print(f"missing advertisements: {set(EXPECTED) - seen}")
        failed = True
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
