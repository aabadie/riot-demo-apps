// SPDX-FileCopyrightText: 2026 Inria
// SPDX-License-Identifier: LGPL-2.1-only
//
// A teapot built from curves: the body and the lid are profiles turned
// around the y axis, the spout and the handle are tubes swept along Bezier
// curves. The spout points to +x, y is up, the teapot is about 1.6 high and
// centered on the origin.

const SIDES = 64;

/** Catmull-Rom points through a polyline, `steps` per segment. */
function smooth(points, steps) {
  const out = [];
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = points[Math.max(i - 1, 0)];
    const p1 = points[i];
    const p2 = points[i + 1];
    const p3 = points[Math.min(i + 2, points.length - 1)];
    for (let s = 0; s < steps; s++) {
      const t = s / steps;
      const t2 = t * t;
      const t3 = t2 * t;
      out.push([0, 1].map((k) => 0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t +
        (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
        (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3)));
    }
  }
  out.push(points[points.length - 1]);
  return out;
}

const bezier = (p, t) => {
  const u = 1 - t;
  return [0, 1].map((k) => u * u * u * p[0][k] + 3 * u * u * t * p[1][k] +
    3 * u * t * t * p[2][k] + t * t * t * p[3][k]);
};

const bezierTangent = (p, t) => {
  const u = 1 - t;
  return [0, 1].map((k) => 3 * u * u * (p[1][k] - p[0][k]) + 6 * u * t * (p[2][k] - p[1][k]) +
    3 * t * t * (p[3][k] - p[2][k]));
};

class Mesh {
  constructor() {
    this.positions = [];
    this.indices = [];
  }

  vertex(x, y, z) {
    this.positions.push(x, y, z);
    return this.positions.length / 3 - 1;
  }

  /** A grid of rows x cols vertices, wrapped around the columns. */
  grid(rows, cols, at) {
    const base = this.positions.length / 3;
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        this.vertex(...at(r, c));
      }
    }
    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < cols; c++) {
        const a = base + r * cols + c;
        const b = base + r * cols + ((c + 1) % cols);
        const d = a + cols;
        const e = b + cols;
        this.indices.push(a, d, e, a, e, b);
      }
    }
  }

  /** A profile [r, y] turned around the y axis. */
  lathe(profile) {
    this.grid(profile.length, SIDES, (r, c) => {
      const a = (2 * Math.PI * c) / SIDES;
      const [rad, y] = profile[r];
      return [rad * Math.cos(a), y, rad * Math.sin(a)];
    });
  }

  /** A tube along a Bezier curve of the xy plane, its half widths in the
      plane and along z given by radius(t). */
  tube(curve, steps, sides, radius) {
    this.grid(steps + 1, sides, (r, c) => {
      const t = r / steps;
      const [px, py] = bezier(curve, t);
      const [tx, ty] = bezierTangent(curve, t);
      const len = Math.hypot(tx, ty);
      // normal of the curve in its plane, z is the other axis of the section
      const nx = -ty / len;
      const ny = tx / len;
      const a = (2 * Math.PI * c) / sides;
      const [rIn, rZ] = radius(t);
      return [px + nx * rIn * Math.cos(a), py + ny * rIn * Math.cos(a), rZ * Math.sin(a)];
    });
  }

  /** Smooth normals: the face normals summed on their vertices. */
  normals() {
    const p = this.positions;
    const n = new Float32Array(p.length);
    for (let i = 0; i < this.indices.length; i += 3) {
      const [a, b, c] = [0, 1, 2].map((k) => this.indices[i + k] * 3);
      const u = [p[b] - p[a], p[b + 1] - p[a + 1], p[b + 2] - p[a + 2]];
      const v = [p[c] - p[a], p[c + 1] - p[a + 1], p[c + 2] - p[a + 2]];
      const f = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
      for (const k of [a, b, c]) {
        n[k] += f[0];
        n[k + 1] += f[1];
        n[k + 2] += f[2];
      }
    }
    for (let i = 0; i < n.length; i += 3) {
      const len = Math.hypot(n[i], n[i + 1], n[i + 2]) || 1;
      n[i] /= len;
      n[i + 1] /= len;
      n[i + 2] /= len;
    }
    return n;
  }
}

const smoothstep = (a, b, t) => {
  const x = Math.max(0, Math.min(1, (t - a) / (b - a)));
  return x * x * (3 - 2 * x);
};

/**
 * The teapot, as triangles: positions and normals (Float32Array of x y z),
 * indices (Uint32Array). The normals face either way, the renderer lights
 * both sides.
 */
export function buildTeapot() {
  const mesh = new Mesh();

  // the body, from the center of the bottom to the inside of the rim
  mesh.lathe(smooth([
    [0, 0], [0.5, 0], [0.78, 0.03], [0.95, 0.16], [1.04, 0.36], [1.06, 0.55],
    [1.0, 0.76], [0.88, 0.94], [0.76, 1.04], [0.72, 1.08], [0.745, 1.11],
    [0.73, 1.135], [0.68, 1.115],
  ], 5));

  // the lid and its knob
  mesh.lathe(smooth([
    [0.7, 1.105], [0.66, 1.17], [0.55, 1.25], [0.36, 1.31], [0.17, 1.34],
    [0.1, 1.37], [0.11, 1.42], [0.17, 1.47], [0.16, 1.53], [0.09, 1.575], [0, 1.585],
  ], 5));

  // the spout, from inside the body to a flared tip
  mesh.tube([[0.8, 0.3], [1.38, 0.28], [1.28, 0.95], [1.74, 1.22]], 48, 28, (t) => {
    const r = 0.26 + (0.075 - 0.26) * t + 0.035 * smoothstep(0.82, 1, t);
    return [r, r];
  });

  // the handle, flatter than wide, its two ends inside the body
  mesh.tube([[-0.8, 0.95], [-1.78, 1.18], [-1.72, 0.18], [-0.86, 0.3]], 48, 20,
    () => [0.068, 0.115]);

  const positions = new Float32Array(mesh.positions);
  // center the teapot on the origin
  for (let i = 1; i < positions.length; i += 3) {
    positions[i] -= 0.79;
  }
  return { positions, normals: mesh.normals(), indices: new Uint32Array(mesh.indices) };
}
