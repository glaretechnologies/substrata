/*=====================================================================
plane.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import { dot3, mulScalar3, sub3 } from './vec3.js';

export interface Plane {
  normal: Float32Array
  D: number
}

export function signedDistToPoint (plane: Plane, point: Float32Array): number {
	return dot3(point, plane.normal) - plane.D;
}

export function closestPointOnPlane (plane: Plane, point: Float32Array, output: Float32Array): Float32Array {
	mulScalar3(plane.normal, signedDistToPoint(plane, point), output); // output = sd * N
	sub3(point, output, output);
	return output;
}
