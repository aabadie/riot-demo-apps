// SPDX-FileCopyrightText: 2026 Inria
// SPDX-License-Identifier: LGPL-2.1-only
//
// Decode the packets printed by test_protocol with the protocol.js of the
// page, check the values, and check the mapping of the board axes to the
// axes of the scene.

import { createInterface } from "node:readline";
import * as P from "../../web/protocol.js";

const near = (a, b, eps = 0.001) => Math.abs(a - b) < eps;
const nearAll = (a, b, eps) => a.length === b.length && a.every((v, i) => near(v, b[i], eps));

const EXPECTED = {
  motion_roll: (v) => {
    const m = P.decodeMotion(v);
    return m.seq === 200 && m.buttonA && !m.buttonB && m.mag && !m.calibrating &&
      nearAll(m.q, [0.7071, 0.7071, 0, 0], 0.001);
  },
  motion_neg: (v) => {
    const m = P.decodeMotion(v);
    /* -1 is just inside the range of Q14 */
    return m.seq === 1 && m.buttonB && m.calibrating && nearAll(m.q, [-1, 0, -0.5, 0.25], 0.001);
  },
  raw: (v) => {
    const r = P.decodeRaw(v);
    return nearAll(r.accel, [0.012, -0.981, 1.5], 0.001) &&
      nearAll(r.gyro, [123.4, -999.9, 0.1], 0.01);
  },
  info: (v) => {
    const i = P.decodeInfo(v);
    return i.version === 1 && i.sensorHz === 104 && i.notifyHz === 50 && i.hasMag && i.axes === 6;
  },
};

// apply a column major 4x4 matrix to a vector
const apply = (m, [x, y, z]) => [0, 1, 2].map((r) => m[r] * x + m[4 + r] * y + m[8 + r] * z);

function checkScene() {
  const s = Math.SQRT1_2;
  const cases = [
    // 90 degrees about the z axis of the board (out of the screen, the
    // board turning on the table): about y in the scene, x goes to -z
    { name: "turn on the table", q: [s, 0, 0, s], v: [1, 0, 0], want: [0, 0, -1] },
    // 90 degrees about the x axis of the board: the top of the screen (y)
    // rises out of the table, in the scene -z goes to y
    { name: "tilt about x", q: [s, s, 0, 0], v: [0, 0, -1], want: [0, 1, 0] },
    // 90 degrees about the y axis: the out of screen axis (scene y) goes to
    // board x, scene x
    { name: "tilt about y", q: [s, 0, s, 0], v: [0, 1, 0], want: [1, 0, 0] },
  ];
  let ok = true;
  for (const c of cases) {
    const got = apply(P.quatToMat4(P.toScene(c.q)), c.v);
    const pass = nearAll(got, c.want, 1e-6);
    console.log(`# scene ${c.name}: ${pass ? "ok" : `FAIL ${got.map((x) => x.toFixed(3))}`}`);
    ok &&= pass;
  }
  const e = P.euler([Math.cos(Math.PI / 9), Math.sin(Math.PI / 9), 0, 0]);
  const pass = near(e.roll, 40, 1e-6) && near(e.pitch, 0, 1e-6) && near(e.yaw, 0, 1e-6);
  console.log(`# euler roll 40: ${pass ? "ok" : "FAIL"}`);
  return ok && pass;
}

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
  const ok = EXPECTED[name](new DataView(bytes.buffer));
  console.log(`${name} ${ok ? "ok" : "FAIL"}`);
  failed ||= !ok;
  seen.add(name);
}
if (Object.keys(EXPECTED).some((k) => !seen.has(k))) {
  console.log("missing packets");
  failed = true;
}
failed ||= !checkScene();
console.log(failed ? "FAIL" : "PASS");
process.exit(failed ? 1 : 0);
