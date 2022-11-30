/*=====================================================================
MeshBuilder.ts
---------------
Copyright Glare Technologies Limited 2022 -

Creates a Batched Mesh or Voxel Mesh from data returned by the Mesh
Loader.
=====================================================================*/

import * as THREE from '../build/three.module.js';
import { createIndex } from '../physics/bvh.js';
import { BMesh, VoxelMesh } from './message.js';

const VertAttribute_Position = 0;
const VertAttribute_Normal = 1;
const VertAttribute_Colour = 2;
const VertAttribute_UV_0 = 3;
const VertAttribute_UV_1 = 4;
const VertAttribute_Joints = 5; // Indices of joint nodes for skinning
const VertAttribute_Weights = 6; // weights for skinning
const MAX_VERT_ATTRIBUTE_TYPE_VALUE = 6;

// The attribute name of the VertAttribute_xxx, joints & weights unused
const attrNameTable = [
	'position',
	'normal',
	'color',
	'uv',
	'uv2'
];

// The attribute component count of the VertAttribute_xxx
const attrComponents = [
	3,
	3,
	3,
	2,
	2,
];

// For now, returns the current dependencies, BufferGeometry and Triangles...
//export function buildBatchedMesh (bmesh: BMeshData): [THREE.BufferGeometry, Triangles] {
export function buildBatchedMesh (bmesh: BMesh): THREE.BufferGeometry {
	const geometry = new THREE.BufferGeometry();

	const groups = new Uint32Array(bmesh.groupsBuffer);

	for (let i = 0; i < groups.length; i += 3) {
		geometry.addGroup(groups[i], groups[i+1], groups[i+2]);
	}

	const index = createIndex(bmesh.indexType, bmesh.indexBuffer);
	geometry.setIndex(new THREE.BufferAttribute(index, 1));

	const interleaved = new Float32Array(bmesh.interleaved);
	const interleavedBuffer = new THREE.InterleavedBuffer(interleaved, bmesh.interleavedStride);

	// Reconstruct the interleaved buffer
	for (let i = 0; i !== bmesh.attributes.length; ++i) {
		const type = bmesh.attributes[i].type;
		const offset = bmesh.attributes[i].offset_B;
		console.assert(0 <= type && type < VertAttribute_Joints);
		const name = attrNameTable[type];
		const itemSize = attrComponents[type];
		geometry.setAttribute(name,	new THREE.InterleavedBufferAttribute(interleavedBuffer, itemSize, offset));
	}

	return geometry;
}

export function buildVoxelMesh(voxelData: VoxelMesh): [THREE.BufferGeometry, number] {
	const geometry = new THREE.BufferGeometry();

	const groups = new Uint32Array(voxelData.groupsBuffer);

	for (let i = 0; i < groups.length; i += 3) {
		geometry.addGroup(groups[i], groups[i+1], groups[i+2]);
	}

	const index = new Uint32Array(voxelData.indexBuffer);
	geometry.setIndex(new THREE.BufferAttribute(index, 1));

	const position = new Float32Array(voxelData.positionBuffer);
	const positionBuffer = new THREE.BufferAttribute(position, 3);
	geometry.setAttribute('position', positionBuffer);

	return [geometry, voxelData.subsample_factor];
}