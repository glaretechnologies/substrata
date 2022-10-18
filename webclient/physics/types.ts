/*=====================================================================
physics/types.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

/*
Common API types used in Physics
*/

import { WorldObject } from '../worldobject.js';

export const POS_X = 0;
export const POS_Y = 1;
export const POS_Z = 2;
export const NOR_X = 3;
export const NOR_Y = 4;
export const NOR_Z = 5;
export const DIST = 6;

export interface SphereTraceResult { // World Space
  data: Float32Array; // [ pos.x, pos.y, pos.z, nor.x, nor.y, nor.z, dist ]
  hit?: WorldObject; // The world object we may have hit
  pointInTri?: boolean; // Is the point inside the triangle (i.e. a face vs edge collision?)
}

export function makeSphereTraceResult(): SphereTraceResult {
  const result = {
    data: new Float32Array(7),
    hit: undefined,
    pointInTri: undefined
  };
  result.data[6] = Number.POSITIVE_INFINITY;
  return result;
}

export function clearSphereTraceResult (result: SphereTraceResult) {
  result.data.fill(0);
  result.data[6] = Number.POSITIVE_INFINITY;
  result.hit = undefined;
}

export interface Ray {
  origin: Float32Array,
  dir: Float32Array,
  minmax: Float32Array
}

export function makeRay (ray?: Ray): Ray {
  return {
    origin: ray ? new Float32Array(ray.origin) : new Float32Array(3),
    dir: ray ? new Float32Array(ray.dir) : new Float32Array(3),
    minmax: ray ? new Float32Array(ray.minmax) : new Float32Array(2)
  };
}
