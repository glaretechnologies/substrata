/*=====================================================================
vec3.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

/*
These are maths routines related to 3D vectors; mostly for operating directly on Float32Arrays
*/

import { EPSILON } from './defs.js';
import { Vector3, Matrix4 } from '../build/three.module.js';

export function fromVector3 (vec: Vector3): Float32Array {
	return new Float32Array([vec.x, vec.y, vec.z]);
}

// Transform the input as a point using the 4x4 matrix (copied from Three.js applyMatrix4)
export function applyMatrix4(mat: Matrix4, v: Float32Array, output?: Float32Array): Float32Array {
	const x = v[0], y = v[1], z = v[2];
	const e = mat.elements;

	// const w = 1.0 / (e[3] * x + e[7] * y + e[11] * z + e[15]);

	output = output == null ? v : output;
	output[0] = (e[0] * x + e[4] * y + e[8] * z + e[12]);
	output[1] = (e[1] * x + e[5] * y + e[9] * z + e[13]);
	output[2] = (e[2] * x + e[6] * y + e[10] * z + e[14]);

	return output;
}

// Transform the input as a vector using the 4x4 matrix (copied from Three.js transformDirection)
export function transformDirection(mat: Matrix4, v: Float32Array, output?: Float32Array): Float32Array {
	const x = v[0], y = v[1], z = v[2];
	const e = mat.elements;

	output = output == null ? v : output;

	output[0] = e[0] * x + e[4] * y + e[8] * z;
	output[1] = e[1] * x + e[5] * y + e[9] * z;
	output[2] = e[2] * x + e[6] * y + e[10] * z;

	return output;
}

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
export function add3 (lhs: Float32Array, rhs: Float32Array, output?: Float32Array): Float32Array {
	output = output ?? lhs;
	output[0] = lhs[0] + rhs[0];
	output[1] = lhs[1] + rhs[1];
	output[2] = lhs[2] + rhs[2];
	return output;
}

// Add the scaled rhs to the lhs and optionally output to new vector
export function addScaled3 (lhs: Float32Array, rhs: Float32Array, scalar: number, output?: Float32Array): Float32Array {
	output = output ?? lhs;
	output[0] = lhs[0] + rhs[0] * scalar;
	output[1] = lhs[1] + rhs[1] * scalar;
	output[2] = lhs[2] + rhs[2] * scalar;
	return output;
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
export function sub3 (lhs: Float32Array, rhs: Float32Array, output?: Float32Array): Float32Array {
	output = output ?? lhs;
	output[0] = lhs[0] - rhs[0];
	output[1] = lhs[1] - rhs[1];
	output[2] = lhs[2] - rhs[2];
	return output;
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
export function mulScalar3 (lhs: Float32Array, s: number, output?: Float32Array): Float32Array {
	output = output ?? lhs;
	output[0] = lhs[0] * s;
	output[1] = lhs[1] * s;
	output[2] = lhs[2] * s;
	return output;
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

// Calculate the squared distance of two 3-component vectors
export function sqDist3(u: Float32Array, v: Float32Array): number {
	const x = u[0] - v[0], y = u[1] - v[1], z = u[2] - v[2];
	return x * x + y * y + z * z;
}

// Squared length of the vector
export function sqLen3 (u: Float32Array): number {
	return dot3(u, u);
}

// Length of the input vector
export function len3 (u: Float32Array): number {
	return Math.sqrt(dot3(u, u));
}

// Normalise a 3-component vector
export function normalise3 (u: Float32Array, output?: Float32Array): Float32Array {
	output = output ?? u;
	const recipLen = 1.0 / Math.sqrt(dot3(u,u));
	output[0] = recipLen * u[0]; output[1] = recipLen * u[1]; output[2] = recipLen * u[2];
	return output;
}

// Remove the input component vector from the input direction vector and return the result
export function removeComponentInDir(comp: Float32Array, dir: Float32Array, output?: Float32Array): Float32Array {
	const tmp = new Float32Array(comp);
	mulScalar3(tmp, dot3(dir, comp));
	return sub3(dir, tmp, output != null ? output : dir);
}

// Return string representation of 3-component vector, for debugging
export function vec3ToString(v: Float32Array): string { return '(' + v[0].toString() + ', ' + v[1].toString() + ', ' + v[2].toString() + ')'; }

