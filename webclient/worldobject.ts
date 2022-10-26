/*=====================================================================
worldobject.ts
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


import * as THREE from './build/three.module.js';
import { BufferIn, readInt32, readUInt32, readUInt64, readFloat, readDouble, readStringFromStream } from './bufferin.js';
import { BufferOut } from './bufferout.js';
import { WorldMaterial, readWorldMaterialFromStream } from './worldmaterial.js';
import {
	Vec2d, Vec3f, Vec3d, readVec2dFromStream, readVec3fFromStream, readVec3dFromStream, Colour3f, Matrix2f, readColour3fFromStream, readMatrix2fFromStream,
	readUserIDFromStream, readTimeStampFromStream, readUIDFromStream, readParcelIDFromStream, writeUID
} from './types.js';
import BVH, { Triangles } from './physics/bvh.js';
import { removeDotAndExtension } from './utils.js';


export const MESH_NOT_LOADED = 0;
export const MESH_LOADING = 1;
export const MESH_LOADED = 2;


const WorldObject_ObjectType_VoxelGroup = 2;


export class WorldObject {
	uid: bigint;
	object_type: number;
	model_url: string;
	mats: Array<WorldMaterial>;
	lightmap_url: string;

	script: string;
	content: string;
	target_url: string;

	audio_source_url: string;
	audio_volume: number;

	pos: Vec3d;
	axis: Vec3f;
	angle: number;

	scale: Vec3f;

	created_time: bigint;
	creator_id: number;

	flags: number;

	creator_name: string;

	aabb_ws_min: Vec3f;
	aabb_ws_max: Vec3f;

	max_model_lod_level: number;

	compressed_voxels: ArrayBuffer;

	bvh: BVH
	get objectToWorld(): THREE.Matrix4 { return this.mesh.matrixWorld; }
	worldToObject: THREE.Matrix4 // TODO: We should only calculate the inverse when the world matrix actually changes
	world_aabb: Float32Array // Root AABB node created from aabb_ws_min & aabb_ws_max

	mesh_state: number;
	mesh: THREE.Mesh;


	current_lod_level: number; // LOD level as a function of distance from camera etc.. Kept up to date.
	in_proximity: boolean; // Used by proximity loader

	constructor() {
		this.mesh_state = MESH_NOT_LOADED;

		this.current_lod_level = 0;
		this.in_proximity = false;
	}

	AABBLongestLength(): number {
		return Math.max(
			this.aabb_ws_max.x - this.aabb_ws_min.x,
			this.aabb_ws_max.y - this.aabb_ws_min.y,
			this.aabb_ws_max.z - this.aabb_ws_min.z
		);
	}

	getLODLevel(campos: THREE.Vector3): number {

		const dist = new THREE.Vector3(this.pos.x, this.pos.y, this.pos.z).distanceTo(campos);
		let proj_len = this.AABBLongestLength() / dist;

		// For voxel objects, push out the transition distances a bit.
		if (this.object_type == WorldObject_ObjectType_VoxelGroup)
			proj_len *= 2;

		if (proj_len > 0.6)
			return -1;
		else if (proj_len > 0.16)
			return 0;
		else if (proj_len > 0.03)
			return 1;
		else
			return 2;
	}

	getLODLevelForCamToObDist2(cam_to_ob_d2: number): number {

		let proj_len = this.AABBLongestLength() / Math.sqrt(cam_to_ob_d2);

		// For voxel objects, push out the transition distances a bit.
		if (this.object_type == WorldObject_ObjectType_VoxelGroup)
			proj_len *= 2;

		if (proj_len > 0.6)
			return -1;
		else if (proj_len > 0.16)
			return 0;
		else if (proj_len > 0.03)
			return 1;
		else
			return 2;
	}

	getModelLODLevel(campos: THREE.Vector3): number { // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
		if (this.max_model_lod_level == 0)
			return 0;

		return Math.max(0, this.getLODLevel(campos));
	}
	
}


export function readWorldObjectFromNetworkStreamGivenUID(buffer_in: BufferIn) {
	let ob = new WorldObject();

	ob.object_type = readUInt32(buffer_in);
	ob.model_url = readStringFromStream(buffer_in);
	// Read mats
	{
		let num = readUInt32(buffer_in);
		if (num > 10000)
			throw "Too many mats: " + num.toString();
		ob.mats = []
		for (let i = 0; i < num; ++i)
			ob.mats.push(readWorldMaterialFromStream(buffer_in));
	}

	ob.lightmap_url = readStringFromStream(buffer_in);

	ob.script = readStringFromStream(buffer_in);
	ob.content = readStringFromStream(buffer_in);
	ob.target_url = readStringFromStream(buffer_in);

	ob.audio_source_url = readStringFromStream(buffer_in);
	ob.audio_volume = readFloat(buffer_in);

	ob.pos = readVec3dFromStream(buffer_in);
	ob.axis = readVec3fFromStream(buffer_in);
	ob.angle = readFloat(buffer_in);

	ob.scale = readVec3fFromStream(buffer_in);

	ob.created_time = readTimeStampFromStream(buffer_in);
	ob.creator_id = readUserIDFromStream(buffer_in);

	ob.flags = readUInt32(buffer_in);

	ob.creator_name = readStringFromStream(buffer_in);

	ob.aabb_ws_min = readVec3fFromStream(buffer_in);
	ob.aabb_ws_max = readVec3fFromStream(buffer_in);

	ob.max_model_lod_level = readInt32(buffer_in);

	if (ob.object_type == WorldObject_ObjectType_VoxelGroup) {
		// Read compressed voxel data
		let voxel_data_size = readUInt32(buffer_in);
		if (voxel_data_size > 1000000)
			throw "Invalid voxel_data_size (too large): " + voxel_data_size.toString();

		if (voxel_data_size > 0) {
			ob.compressed_voxels = buffer_in.readData(voxel_data_size); // Read voxel data
		}
	}

	return ob;
}


export function getLODModelURLForLevel(base_model_url: string, level: number): string {
	if (level <= 0)
		return base_model_url;
	else {
		if (base_model_url.startsWith('http:') || base_model_url.startsWith('https:'))
			return base_model_url;

		if (level == 1)
			return removeDotAndExtension(base_model_url) + '_lod1.bmesh'; // LOD models are always saved in BatchedMesh (bmesh) format.
		else
			return removeDotAndExtension(base_model_url) + '_lod2.bmesh';
	}
}
