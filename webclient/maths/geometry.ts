/*=====================================================================
geometry.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// Geometric objects & queries defined using typed arrays rather than Three.js vectors

import { EPSILON } from './defs.js';
import * as THREE from '../build/three.module.js';
import { max3, min3 } from './vec3.js';

// Create a sphere in a 4 component Float32Array
export function makeSphere (pos: THREE.Vector3, radius: number): Float32Array {
	return new Float32Array([pos.x, pos.y, pos.z, radius]);
}

// Create an AABB in a Float32Array [ minx, miny, minz, maxx, maxy, maxz ]
export function makeAABB (min: THREE.Vector3, max: THREE.Vector3): Float32Array {
	return new Float32Array([min.x, min.y, min.z, max.x, max.y, max.z]);
}

// Transform an AABB into a different space and recompute the AABB in the new space - note: recompute from local AABB!
export function transformAABB (mat: THREE.Matrix4, aabb: Float32Array, output: Float32Array): Float32Array {
	const el = mat.elements;
	const idx = (row, col) => 4 * col + row;

	for(let i = 0; i !== 3; ++i) {
		output[i] = output[3+i] = el[idx(i, 3)]; // Add the translation of the matrix to the output bound

		for(let j = 0; j !== 3; ++j) { // Perform the rotation in place and calculate the new extents
			const s = el[idx(i, j)];
			const e = s * aabb[j], f = s * aabb[j+3];
			if(e < f) {
				output[i] += e;
				output[i+3] += f;
			} else {
				output[i] += f;
				output[i+3] += e;
			}
		}
	}

	return output;
}

// Form the union between two AABBs
export function unionAABB (lhs: Float32Array, rhs: Float32Array, output?: Float32Array): Float32Array {
	output = output != null ? output : new Float32Array(6);
	output.set(lhs);
	min3(output, 0, rhs, 0);
	max3(output, 3, rhs, 3);
	return output;
}

// Build an AABB from a sphere and a vector representing the path of the sphere
export function spherePathToAABB (sphere: Float32Array, translation: Float32Array): Float32Array {
	const [cx, cy, cz, r] = sphere;
	const tx = cx + translation[0], ty = cy + translation[1], tz = cz + translation[2];
	return new Float32Array([
		Math.min(cx, tx) - r, Math.min(cy, ty) - r, Math.min(cz, tz) - r,
		Math.max(cx, tx) + r, Math.max(cy, ty) + r, Math.max(cz, tz) + r
	]);
}

// Test Two AABBs for intersection
export function testAABB (lhs: Float32Array, rhs: Float32Array): boolean {
	if (lhs[3] < rhs[0] || lhs[0] > rhs[3]) return false;
	if (lhs[4] < rhs[1] || lhs[1] > rhs[4]) return false;
	return !(lhs[5] < rhs[2] || lhs[2] > rhs[5]);
}

// Test AABBs for intersection in place
export function testAABBV (lhs: Float32Array, loff: number, rhs: Float32Array, roff=0): boolean {
	if (lhs[loff+3] < rhs[roff] || lhs[loff] > rhs[roff+3]) return false;
	if (lhs[loff+4] < rhs[roff+1] || lhs[loff+1] > rhs[roff+4]) return false;
	return !(lhs[loff+5] < rhs[roff+2] || lhs[loff+2] > rhs[roff+5]);
}

// Test Ray against AABB
export function testRayAABB (OP: Float32Array, d: Float32Array, aabb: Float32Array, off=0): [boolean, number] | null {
	let t_min = 0;
	let t_max = Number.MAX_VALUE;
	const min = aabb.slice(off, off+3);
	const max = aabb.slice(off+3, off+6);

	for (let i = 0; i < 3; ++i) {
		if(Math.abs(d[i]) < EPSILON) {
			if (OP[i] < min[i] || OP[i] > max[i]) return [false, Number.POSITIVE_INFINITY];
		} else {
			const invD = 1. / d[i];
			let t0 = (min[i] - OP[i]) * invD;
			let t1 = (max[i] - OP[i]) * invD;
			if (t0 > t1) {
				const tmp = t0; t0 = t1; t1 = tmp;
			}
			t_min = Math.max(t_min, t0);
			t_max = Math.min(t_max, t1);

			if(t_min > t_max) return [false, Number.POSITIVE_INFINITY];
		}
	}

	return [true, t_min];
}

// Get squared distance from point to AABB
export function sqDistPointAABB (point: Float32Array, aabb: Float32Array): number {
	let distSq = 0.;
	const min = 0, max = 3;
	for(let i = 0; i < 3; ++i) {
		if(point[i] < aabb[min+i]) distSq += (aabb[min+i] - point[i]) * (aabb[min+i] - point[i]);
		if(point[i] > aabb[max+i]) distSq += (point[i] - aabb[max+i]) * (point[i] - aabb[max+i]);
	}
	return distSq;
}

// Get squared distance from sphere to AABB
export function sqDistSphereAABB (sphere: Float32Array, aabb: Float32Array): number {
	const distSq = sqDistPointAABB(sphere, aabb), sq = sphere[3] * sphere[3];
	return distSq < sq ? 0 : distSq - sq;
}

// Determine whether a sphere and AABB intersect or not
export function testSphereAABB (sphere: Float32Array, aabb: Float32Array): boolean {
	return sqDistPointAABB(sphere, aabb) < sphere[3] * sphere[3];
}
