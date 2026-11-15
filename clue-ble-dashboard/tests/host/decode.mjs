// SPDX-FileCopyrightText: 2026 Inria
// SPDX-License-Identifier: LGPL-2.1-only
//
// Decode the packets printed by test_protocol with the protocol.js of the
// page and check the values.

import { createInterface } from "node:readline";
import * as P from "../../web/protocol.js";

const near = (a, b) => (a === null || b === null ? a === b : Math.abs(a - b) < 0.006);

const EXPECTED = {
  sensors_full: (s) => near(s.temp, 22.57) && near(s.hum, 41.25) && near(s.press, 1012.3) &&
    s.light === 87 && near(s.accel[0], 0.012) && near(s.accel[1], -0.981) &&
    near(s.accel[2], 1.5) && s.rgb.join() === "120,340,65534",
  sensors_none: (s) => s.accel === null && s.temp === null && s.hum === null &&
    s.press === null && s.light === null && s.rgb === null,
  sensors_cold: (s) => near(s.temp, -12.34),
  status_match: (s) => s.match === 2 && near(s.distance, 1.3) && s.rssi === -63 &&
    s.proximity === 255 && s.counts.join() === "10,0,20,255,0,1" && !s.buttonA && s.buttonB &&
    s.teaching,
  status_far: (s) => s.match === P.MATCH_FAR && s.rssi === null && !s.buttonA &&
    !s.teaching,
};

let failed = false;
const seen = new Set();
for await (const line of createInterface({ input: process.stdin })) {
  if (line.startsWith("#")) {
    console.log(line);
    failed ||= line.includes("FAIL");
    continue;
  }
  const [name, hex] = line.split(" ");
  const bytes = new Uint8Array(hex.match(/../g).map((b) => parseInt(b, 16)));
  const view = new DataView(bytes.buffer);
  const value = name.startsWith("sensors") ? P.decodeSensors(view) : P.decodeStatus(view);
  const ok = EXPECTED[name](value);
  console.log(`${name}: ${JSON.stringify(value)} ${ok ? "ok" : "FAIL"}`);
  failed ||= !ok;
  seen.add(name);
}
if (seen.size !== Object.keys(EXPECTED).length) {
  console.log("missing packets");
  failed = true;
}
const tone = P.encodeTone(880, 250);
const toneOk = tone.join() === "112,3,250,0";
console.log(`tone ${tone.join()} ${toneOk ? "ok" : "FAIL"}`);
failed ||= !toneOk;
console.log(failed ? "FAIL" : "PASS");
process.exit(failed ? 1 : 0);
