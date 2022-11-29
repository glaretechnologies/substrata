/*=====================================================================
debug.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// Debug functions that can likely be removed once no longer useful

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

	return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: 'red' }));
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
		mesh: new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: 'orange' })),
		max,
		buffer,
		geo
	};
}

/*
Probably safe to remove...

// Build a visualisation of a leaves-only BVH tree
export function buildBVHMesh (bvh: BVH): THREE.LineSegments {
	const icount = bvh.nodeCount * 24; // Max possible nodes, TODO optimise this
	const buf = new Float32Array(icount);
	const idx = new Uint32Array(icount);

	let used = 0;
	// Use a queue rather than recursion to traverse the tree.
	const queue = [0];
	while(queue.length > 0 && used < this.nodeCount) {
		const node = queue.shift();
		const offset = this.dataBuffer[2 * node], count = this.dataBuffer[2 * node + 1];
		if(count === 0) { // If count is zero, this is a branch so push to two consecutive child nodes
			queue.push(offset, offset+1);
		} else {
			// Build an AABB box into the mesh
			this.buildAABB(node, buf, idx, 24 * used);
			used += 1;
		}
	}

	const geo = new THREE.BufferGeometry();
	geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false));
	geo.setIndex(new THREE.BufferAttribute(idx, 1));

	// We only use the used number of leaf nodes thus wasting some memory
	// TODO: to be optimised
	geo.setDrawRange(0, used*24);

	return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: 'green' }));
}

// Build an AABB mesh on the root of the BVH
public getRootAABBMesh (): THREE.LineSegments {
	const buf = new Float32Array(24);
	const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(0, 6);

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

	return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: 'red' }));
}

// Update a single AABB mesh with updated data
public updateAABBMesh (mesh: THREE.LineMesh, idx: number): void {
	const attr = mesh.geometry.getAttribute('position');
	if(!attr) return;

const buf = attr.array;
const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(6*idx, 6*(idx+1));
let i = 0;
buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz;
buf[i++] = minx; buf[i++] = miny; buf[i++] = minz;
buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz;
buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz;
buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz;
buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz;
buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz;
buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz;

attr.needsUpdate = true;
}

// Create a triangle highlighter to highlight the outline of triangles on the mesh represented by this BVH
public getTriangleHighlighter (): THREE.LineLoop {
	const buffer = new Float32Array(9);
	const geo = new THREE.BufferGeometry();
	geo.setAttribute('position', new THREE.BufferAttribute(buffer, 3, false));
	const mat = new THREE.LineBasicMaterial({ color: 'blue', depthTest: false });
	return new THREE.LineLoop(geo, mat);
}

// Update the triangle highlighter to the triangle represented by triangleIdx
public updateTriangleHighlighter (triangleIdx: number, mesh: THREE.LineLoop): void {
	const attr = mesh.geometry.getAttribute('position');
	if(!attr) return;

const buffer = attr.array;
const [v0, v1, v2] = this.tmp;
this.tri.getTriVertex(triangleIdx, 0, v0);
this.tri.getTriVertex(triangleIdx, 1, v1);
this.tri.getTriVertex(triangleIdx, 2, v2);
buffer.set(v0, 0); buffer.set(v1, 3); buffer.set(v2, 6);
attr.needsUpdate = true;
}

*/