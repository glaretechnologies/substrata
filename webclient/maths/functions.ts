/*
Miscellaneous maths functions that are missing from Three.js or the definition of which I prefer over the one in Three.
*/

import { EPSILON } from './defs.js';

// Compare two floating point numbers for equality
export function eq (a: number, b: number, epsilon=EPSILON): boolean {
  return Math.abs(b - a) <= epsilon;
}

export function lerpN (A: number, B: number, u: number): number {
  return (1.0 - u) * A + u * B;
}

export function lerpA (A: ArrayLike<number>, B: ArrayLike<number>, u: number, output: Float32Array | number[]): Float32Array | number[] {
  const len = output.length;
  const mu = 1.0 - u;
  for(let i = 0; i < len; ++i) output[i] = mu * A[i] + u * B[i];
  return output;
}

export function absA (A: Float32Array | number[], len=3): Float32Array | number[] {
  for(let i = 0; i < len; ++i) A[i] = Math.abs(A[i]);
  return A;
}

// Generate a sequence of numbers from offset to count in an Uint32Array
export function range (count: number, offset=0): Uint32Array {
  const r = new Uint32Array(count);
  if(offset === 0) {
    for(let i = 0; i !== count; ++i) r[i] = i;
  } else {
    for(let i = 0; i !== count; ++i) r[i] = i + offset;
  }

  return r;
}

// Easier print function that outputs numbers to 3 decimal points.
export function print3 (...args: (Float32Array | string | number)[]): void {
  let out = '';
  for(let i = 0; i !== args.length; ++i) {
    if(typeof args[i] === 'string') out += `${args[i]}: `;
    else if(typeof args[i] === 'number') out += `${Number(args[i]).toFixed(3)},`;
    else {
      out += `[${Number(args[i][0]).toFixed(3)}, `;
      out += `${Number(args[i][1]).toFixed(3)}, `;
      out += `${Number(args[i][2]).toFixed(3)}], `;
    }
  }
  console.log(out);
}
