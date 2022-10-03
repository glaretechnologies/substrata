/*
Common API types used in Physics
*/

import { WorldObject } from '../webclient.js';

export interface SphereTraceResult { // World Space
  data: Float32Array; // [ pos.x, pos.y, pos.z, nor.x, nor.y, nor.z, dist ]
  hit?: WorldObject; // The world object we may have hit
  pointInTri?: boolean; // Is the point inside the triangle (i.e. a face vs edge collision?)
}
