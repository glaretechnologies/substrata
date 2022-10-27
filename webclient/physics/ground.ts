/*=====================================================================
ground.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as THREE from '../build/three.module.js';
import BVH, { Triangles } from './bvh.js';
import { generatePatch } from '../maths/generators.js';
import { MAX_CAM_DIST } from '../cameraController.js';

/*
Adds a virtual ground plane to the physics world
*/
export class Ground {
	private readonly bvh_: BVH;
	private readonly mesh_: THREE.Mesh;

	private readonly worldToObject_: THREE.Matrix4;

	public constructor () {
		const geo = generatePatch(1, 1);
		const buf = new THREE.BufferGeometry();
		buf.setAttribute('position', new THREE.BufferAttribute(geo[0], 3, false));
		buf.setIndex(new THREE.BufferAttribute(geo[1], 1, false));
		this.mesh_ = new THREE.Mesh(buf, new THREE.MeshBasicMaterial({ color: 'red', transparent: true, opacity: 0.5 }));
		this.mesh_.position.set(0, 0, 0);
		// Needs to be large enough for the 3rd person camera ray test
		this.mesh_.scale.set(MAX_CAM_DIST+1, MAX_CAM_DIST+1, MAX_CAM_DIST+1);

		const triangles = new Triangles(geo[0], geo[1], 3); // Stride = 3 (multiples of 4 bytes) = 12 bytes per vertex
		this.bvh_ = new BVH(triangles);

		this.worldToObject_ = new THREE.Matrix4();

		this.mesh_.visible = false;
	}

	// In order to visualise the ground collision plane
	public get mesh (): THREE.Mesh { return this.mesh_; }
	public get bvh (): BVH { return this.bvh_; }
	public get worldToObject (): THREE.Matrix4 { return this.worldToObject_; }
	public get objectToWorld (): THREE.Matrix4 { return this.mesh_.matrixWorld; }

	// Implemented method in MainWindow::UpdateGroundPlane
	public updateGroundPlane (camPos: Float32Array): void {
		this.mesh.position.set(camPos[0], camPos[1], this.mesh_.visible ? 1e-3 : 0);
		this.worldToObject.copy(this.mesh_.matrixWorld); // Copy the objectToWorld matrix of the THREE.js mesh and invert
		this.worldToObject.invert(); // TODO: Optimise
	}
}
