// SPDX-FileCopyrightText: 2026 Inria
// SPDX-License-Identifier: LGPL-2.1-only
//
// Packets of the GATT service of the CLUE BLE Dashboard, see protocol.h.

export const uuid = (x) => `c1ae00${x.toString(16).padStart(2, "0")}-5d8a-4a43-9a3c-6f5c2b1e7d90`;

export const UUID = {
  service: uuid(0x01),
  sensors: uuid(0x02),
  status: uuid(0x03),
  text: uuid(0x04),
  teach: uuid(0x05),
  tone: uuid(0x06),
};

export const SLOTS = 6;
export const MATCH_NONE = -1;
export const MATCH_FAR = -2;
export const TEACH_SAMPLES = 1;
export const TEACH_CLEAR = 2;
export const TEACH_CLEAR_ALL = 3;
export const TEXT_MAX = 60;

const INVALID_S16 = -32768;
const INVALID_U16 = 0xffff;

const s16 = (v, f) => (v === INVALID_S16 ? null : v / f);
const u16 = (v, f) => (v === INVALID_U16 ? null : v / f);

/** Decode a sensors packet (a DataView of 20 bytes). */
export function decodeSensors(view) {
  const accel = [0, 2, 4].map((o) => s16(view.getInt16(o, true), 1000));
  const rgb = [14, 16, 18].map((o) => u16(view.getUint16(o, true), 1));
  const press = u16(view.getUint16(10, true), 10);
  return {
    accel: accel.includes(null) ? null : accel,
    temp: s16(view.getInt16(6, true), 100),
    hum: u16(view.getUint16(8, true), 100),
    press,
    light: u16(view.getUint16(12, true), 1),
    rgb: rgb.includes(null) ? null : rgb,
  };
}

/** Decode a status packet (a DataView of 11 bytes). */
export function decodeStatus(view) {
  const counts = [];
  for (let i = 0; i < SLOTS; i++) {
    counts.push(view.getUint8(4 + i));
  }
  const buttons = view.getUint8(10);
  return {
    match: view.getInt8(0),
    distance: view.getUint8(1) / 10,
    rssi: view.getInt8(2) || null,
    proximity: view.getUint8(3),
    counts,
    buttonA: (buttons & 1) !== 0,
    buttonB: (buttons & 2) !== 0,
    teaching: (buttons & 4) !== 0,
  };
}

/** Teach command: 10 samples to a slot, forget a slot or all of them. */
export const encodeTeach = (cmd, slot) => new Uint8Array([cmd, slot]);

/** Note to play on the buzzer. */
export function encodeTone(freq, ms) {
  const buf = new DataView(new ArrayBuffer(4));
  buf.setUint16(0, freq, true);
  buf.setUint16(2, ms, true);
  return new Uint8Array(buf.buffer);
}

/** Text for the screen, the board keeps the first 60 characters. */
export const encodeText = (text) => new TextEncoder().encode(text.slice(0, TEXT_MAX));

/** Rough distance from the RSSI, for a 0 dBm board: about -59 dBm at 1 m. */
export function distanceFromRssi(rssi, atOneMeter = -59, exponent = 2.2) {
  if (rssi === null) {
    return null;
  }
  return 10 ** ((atOneMeter - rssi) / (10 * exponent));
}

/** Screen color of a reading, white balanced on its brightest channel. */
export function rgbToCss(rgb) {
  if (!rgb) {
    return "transparent";
  }
  const max = Math.max(...rgb, 1);
  const [r, g, b] = rgb.map((v) => Math.round((v * 255) / max));
  return `rgb(${r}, ${g}, ${b})`;
}
