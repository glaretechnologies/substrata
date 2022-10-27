/*=====================================================================
avatar.ts
---------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


import { Vec3f, Vec3d, readVec3fFromStream, readVec3dFromStream, Colour3f, Matrix2f, readColour3fFromStream, readMatrix2fFromStream } from './types.js';
import { MESH_NOT_LOADED } from './worldobject.js';
import { BufferIn, readUInt32, readFloat, readStringFromStream } from './bufferin.js';
import { BufferOut } from './bufferout.js';
import { WorldMaterial, readWorldMaterialFromStream } from './worldmaterial.js';
import * as THREE from './build/three.module.js';


export class AvatarSettings {
	model_url: string;
	materials: Array<WorldMaterial>;
	pre_ob_to_world_matrix: Array<number>;

	constructor() {
		this.model_url = "";
		this.materials = [];
		this.pre_ob_to_world_matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];
	}

	writeToStream(stream: BufferOut) {
		stream.writeStringLengthFirst(this.model_url);

		// Write materials
		stream.writeUInt32(this.materials.length)
		for (let i = 0; i < this.materials.length; ++i)
			this.materials[i].writeToStream(stream)

		for (let i = 0; i < 16; ++i)
			stream.writeFloat(this.pre_ob_to_world_matrix[i])
	}


	readFromStream(stream: BufferIn) {
		this.model_url = stream.readStringLengthFirst();

		// Read materials
		let num_mats = stream.readUInt32();
		if (num_mats > 10000)
			throw "Too many mats: " + num_mats
		this.materials = [];
		for (let i = 0; i < num_mats; ++i) {
			let mat = readWorldMaterialFromStream(stream);
			this.materials.push(mat);
		}

		for (let i = 0; i < 16; ++i)
			this.pre_ob_to_world_matrix[i] = stream.readFloat();
	}
}

export class Avatar {
	uid: bigint;
	name: string;
	pos: Vec3d;
	rotation: Vec3f;
	avatar_settings: AvatarSettings;

	anim_state: number;


	mesh_state: number;
	mesh: THREE.Mesh;
	loaded_mesh_URL: string; // The URL of the loaded mesh.  This can be different from model_url as it may have a LOD suffix.

	constructor() {
		//this.uid = BigInt(0); // uint64   // TEMP
		this.name = "";
		this.pos = new Vec3d(0, 0, 0);
		this.rotation = new Vec3f(0, 0, 0);
		this.avatar_settings = new AvatarSettings();

		this.mesh_state = MESH_NOT_LOADED;
	}

	writeToStream(stream: BufferOut) {
		stream.writeUInt64(this.uid);
		stream.writeStringLengthFirst(this.name);
		this.pos.writeToStream(stream);
		this.rotation.writeToStream(stream);
		this.avatar_settings.writeToStream(stream);
	}

	readFromStream(stream: BufferIn) {
		this.uid = stream.readUInt64();
		this.readFromStreamGivenUID(stream);
	}

	readFromStreamGivenUID(stream: BufferIn) {
		this.name = stream.readStringLengthFirst();
		this.pos = readVec3dFromStream(stream);
		this.rotation = readVec3fFromStream(stream);
		this.avatar_settings.readFromStream(stream);
	}
}
