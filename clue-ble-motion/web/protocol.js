// SPDX-FileCopyrightText: 2026 Inria
// SPDX-License-Identifier: LGPL-2.1-only
//
// Packets of the GATT service of CLUE BLE Motion, see protocol.h.

export const uuid = (x) => `c1ae01${x.toString(16).padStart(2, "0")}-5d8a-4a43-9a3c-6f5c2b1e7d90`;

export const UUID = {
  service: uuid(0x01),
  info: uuid(0x02),
  motion: uuid(0x03),
  raw: uuid(0x04),
  control: uuid(0x05),
};

export const CMD_RECENTER = 1;
export const CMD_MODE = 2;
export const CMD_RATE = 3;
export const CMD_CALIBRATE = 4;
export const RATES = [25, 50, 100];

/** Decode a motion packet (a DataView of 10 bytes). */
export function decodeMotion(view) {
  const flags = view.getUint8(1);
  return {
    seq: view.getUint8(0),
    buttonA: (flags & 1) !== 0,
    buttonB: (flags & 2) !== 0,
    mag: (flags & 4) !== 0,
    calibrating: (flags & 8) !== 0,
    q: [2, 4, 6, 8].map((o) => view.getInt16(o, true) / 16384),
  };
}

/** Decode a raw packet (a DataView of 12 bytes): g and degrees per second. */
export function decodeRaw(view) {
  return {
    accel: [0, 2, 4].map((o) => view.getInt16(o, true) / 1000),
    gyro: [6, 8, 10].map((o) => view.getInt16(o, true) / 10),
  };
}

/** Decode the info packet (a DataView of 6 bytes). */
export function decodeInfo(view) {
  return {
    version: view.getUint8(0),
    sensorHz: view.getUint16(1, true),
    notifyHz: view.getUint8(3),
    hasMag: (view.getUint8(4) & 1) !== 0,
    axes: view.getUint8(5),
  };
}

export const encodeRecenter = () => new Uint8Array([CMD_RECENTER]);
export const encodeMode = (axes) => new Uint8Array([CMD_MODE, axes]);
export const encodeRate = (hz) => new Uint8Array([CMD_RATE, hz]);
export const encodeCalibrate = () => new Uint8Array([CMD_CALIBRATE]);

/**
 * The orientation in the axes of the scene, y up: the x axis of the board
 * stays x, its z axis (out of the screen) becomes y and its y axis -z.
 * This is a proper rotation of the axes, so the quaternion keeps its angle
 * and only its axis is mapped.
 */
export function toScene([w, x, y, z]) {
  return [w, x, z, -y];
}

/** Roll, pitch and yaw of a quaternion of the board, in degrees. */
export function euler([w, x, y, z]) {
  const deg = 180 / Math.PI;
  const s = Math.max(-1, Math.min(1, 2 * (w * y - z * x)));
  return {
    roll: Math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * deg,
    pitch: Math.asin(s) * deg,
    yaw: Math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * deg,
  };
}
