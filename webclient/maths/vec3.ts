/*
These are maths routines related to 3D vectors; mostly for operating directly on Float32Arrays
*/

import { EPSILON } from './defs.js';

// Compare current against compare array and store the minimum in current
export function min3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff=0): Float32Array {
  curr[coff] = Math.min(curr[coff], cmp[cmpoff]); coff++;
  curr[coff] = Math.min(curr[coff], cmp[cmpoff+1]); coff++;
  curr[coff] = Math.min(curr[coff], cmp[cmpoff+2]); coff++;
  return curr;
}

// Compare current against compare array and store the maximum in current
export function max3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff=0): Float32Array {
  curr[coff] = Math.max(curr[coff], cmp[cmpoff]); coff++;
  curr[coff] = Math.max(curr[coff], cmp[cmpoff+1]); coff++;
  curr[coff] = Math.max(curr[coff], cmp[cmpoff+2]); coff++;
  return curr;
}

// Compare two 3-component arrays for equality
export function eq3 (a: Float32Array, b: Float32Array, epsilon=EPSILON): boolean {
  for (let i = 0; i < 3; ++i) {
    if (Math.abs(b[i] - a[i]) > epsilon) return false;
  }
  return true;
}

// Add 3-component arrays in place
export function add3V (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] + rhs[roff];
  out[off+1] = lhs[loff+1] + rhs[roff+1];
  out[off+2] = lhs[loff+2] + rhs[roff+2];
  return out;
}

// Add 3-component vectors
export function add3 (lhs: Float32Array, rhs: Float32Array, out: Float32Array): Float32Array {
  out[0] = lhs[0] + rhs[0];
  out[1] = lhs[1] + rhs[1];
  out[2] = lhs[2] + rhs[2];
  return out;
}

// Subtract 3-component arrays in place
export function sub3V (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] - rhs[roff];
  out[off+1] = lhs[loff+1] - rhs[roff+1];
  out[off+2] = lhs[loff+2] - rhs[roff+2];
  return out;
}

// Subtract 3-component vectors
export function sub3 (lhs: Float32Array, rhs: Float32Array, out: Float32Array): Float32Array {
  out[0] = lhs[0] - rhs[0];
  out[1] = lhs[1] - rhs[1];
  out[2] = lhs[2] - rhs[2];
  return out;
}

// Multiply 3-component by scalar in place
export function mulScalar3V(
  lhs: Float32Array, loff: number,
  s: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] * s;
  out[off+1] = lhs[loff+1] * s;
  out[off+2] = lhs[loff+2] * s;
  return out;
}

// Multiply 3-component vectors by scalar
export function mulScalar3 (lhs: Float32Array, s: number, out: Float32Array): Float32Array {
  out[0] = lhs[0] * s;
  out[1] = lhs[1] * s;
  out[2] = lhs[2] * s;
  return out;
}

// Compute 3-component cross product
export function cross3 (u: Float32Array, v: Float32Array, out: Float32Array): Float32Array {
  out[0] = u[1] * v[2] - u[2] * v[1];
  out[1] = u[2] * v[0] - u[0] * v[2];
  out[2] = u[0] * v[1] - u[1] * v[0];
  return out;
}

// Compute 3-component dot product
export function dot3 (u: Float32Array, v: Float32Array): number {
  return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
}
