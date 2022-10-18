/*=====================================================================
triangle.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// Implements the jscol_triangle functions

import { add3, cross3, dot3, mulScalar3, sqDist3, sqLen3, sub3 } from './vec3.js';

export interface Triangle {
  v0: Float32Array
  e0: Float32Array
  e1: Float32Array
  normal: Float32Array
}

const tmp = [
	new Float32Array(3),
	new Float32Array(3),
	new Float32Array(3),
	new Float32Array(3),
	new Float32Array(3),
];

export function pointInTri (tri: Triangle, point: Float32Array): boolean {
	const [e0_normal, e2, v1, e1_normal, e2_normal] = tmp;

	cross3(tri.e0, tri.normal, e0_normal); // e0_normal = tri.e0 x tri.normal
	const d0 = dot3(tri.v0, e0_normal);
	if(dot3(point, e0_normal) > d0) return false;

	sub3(tri.e1, tri.e0, e2); // e2 = tri.e1 - tri.e0
	add3(tri.v0, tri.e0, v1); // v1 = tri.v0 + tri.e0
	cross3(e2, tri.normal, e1_normal); // e1_normal = e2 x tri.normal
	const d1 = dot3(v1, e1_normal);
	if(dot3(point, e1_normal) > d1) return false;

	cross3(tri.normal, tri.e1, e2_normal);
	const d2 = dot3(tri.v0, e2_normal);
	return dot3(point, e2_normal) <= d2;
}

const ap = new Float32Array(3);

export function closestPointOnLine (a: Float32Array, dir: Float32Array, P: Float32Array, out: Float32Array): Float32Array {
	sub3(P, a, ap); // ap = P - a
	const d = dot3(ap, dir);
	if(d <= 0) return a;

	const t = Math.min(1., d / (sqLen3(dir)));
	mulScalar3(dir, t, out); // out = dir * t
	add3(out, a); // out += a
	return out;
}

export function closest (a: Float32Array, b: Float32Array, c: Float32Array, P: Float32Array): Float32Array {
	const d1 = sqDist3(P, a);
	const d2 = sqDist3(P, b);
	const d3 = sqDist3(P, c);

	if(d1 < d2) {
		if(d1 < d3) return a;
		else return c;
	} else {
		if(d2 < d3) return b;
		else return c;
	}
}

export function closestPointOnTriangle (tri: Triangle, point: Float32Array, output: Float32Array): Float32Array {
	const [e0_closest, v1, e2, e1_closest, e2_closest] = tmp;

	closestPointOnLine(tri.v0, tri.e0, point, e0_closest); // e0_closest = closestPointOnLine
	add3(tri.v0, tri.e0, v1);
	sub3(tri.e1, tri.e0, e2);
	closestPointOnLine(v1, e2, point, e1_closest);
	closestPointOnLine(tri.v0, tri.e1, point, e2_closest);
	output.set(closest(e0_closest, e1_closest, e2_closest, point));
	return output;
}