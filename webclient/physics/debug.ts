import * as THREE from '../build/three.module.js';

// Used for visualising the World Space AABBs
export function createAABBMesh (aabb: Float32Array): THREE.LineSegments {
  const buf = new Float32Array(24);
  const [minx, miny, minz, maxx, maxy, maxz] = aabb;

  let i = 0;
  buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz; // 0
  buf[i++] = minx; buf[i++] = miny; buf[i++] = minz; // 1
  buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz; // 2
  buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz; // 3
  buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz; // 4
  buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz; // 5
  buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz; // 6
  buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz; // 7

  const idx = new Uint8Array(24);

  i = 0;
  // left
  idx[i++] = 0; idx[i++] = 1; idx[i++] = 1; idx[i++] = 3;
  idx[i++] = 0; idx[i++] = 2; idx[i++] = 2; idx[i++] = 3;
  // right
  idx[i++] = 4; idx[i++] = 5; idx[i++] = 5; idx[i++] = 7;
  idx[i++] = 4; idx[i++] = 6; idx[i++] = 6; idx[i++] = 7;
  // front
  idx[i++] = 0; idx[i++] = 4; idx[i++] = 2; idx[i++] = 6;
  // back
  idx[i++] = 1; idx[i++] = 5; idx[i++] = 3; idx[i++] = 7;

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false));
  geo.setIndex(new THREE.BufferAttribute(idx, 1));

  return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'red'}));
}

// A small object used for rendering collision response vectors and other segments
export interface SegmentBatch {
  mesh: THREE.LineSegments
  max: number
  buffer: Float32Array
  geo: THREE.BufferGeometry
}

// Used for visualising the normals/collision responses
export function createSegmentsMesh (max: number): SegmentBatch {
  const buffer = new Float32Array(3 * max);
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(buffer, 3, false));

  return {
    mesh: new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'orange'})),
    max,
    buffer,
    geo
  };
}
