/*=====================================================================
caster.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

/*
Ray casting functions optimised to use the physics/collision detection geometry
*/

import * as THREE from '../build/three.module.js';
import { lerpN } from '../maths/functions.js';
import BVH from './bvh.js';
import CameraController from '../cameraController';
import { add3, addScaled3, applyMatrix4, cross3, mulScalar3, normalise3, transformDirection } from '../maths/vec3.js';
import { DEG_TO_RAD } from '../maths/defs.js';

const LEFT = 0;
const RIGHT = 1;

export default class Caster {
	private readonly rndr: THREE.WebGLRenderer;
	private readonly controller: CameraController;

	private readonly dims = new THREE.Vector2();
	private readonly theta: number;
	private readonly htan: number;

	private readonly frustum = new Float32Array(2);
	private U = new Float32Array(3);

	public constructor (renderer: THREE.WebGLRenderer, controller: CameraController) {
		this.rndr = renderer;
		this.controller = controller;

		this.theta = .5 * controller.camSettings.horizontal_fov * DEG_TO_RAD;
		this.htan = Math.tan(this.theta);
		this.frustum[LEFT] = controller.camSettings.near * this.htan;
		this.frustum[RIGHT] = - this.frustum[LEFT];
	}

	// Calculate a pick ray based on the current camera view at screen coordinates [x, y]
	public getPickRay (x: number, y: number): [Float32Array, Float32Array] | null {
		const dPR = this.rndr.getPixelRatio();
		this.rndr.getSize(this.dims);
		if(x < 0 || x > this.dims.x || y < 0 || y > this.dims.y) return null;

		const invH = 1. / this.dims.y;
		const AR = this.dims.x * invH;
		const r = dPR * x / this.dims.x, u = (this.dims.y - dPR * y) * invH;
		const top = this.frustum[LEFT] / AR; const bottom = -top;

		const U = this.U;

		const R = new Float32Array(this.controller.camRightVec);
		const D = new Float32Array(this.controller.camForwardsVec);

		cross3(D, R, U); // U = D x R
		mulScalar3(R, lerpN(this.frustum[LEFT], this.frustum[RIGHT], r)); // R *= lerp(left, right)
		mulScalar3(U, lerpN(top, bottom, u)); // U *= lerp(top, bottom)

		const dir = addScaled3(add3(R, U), D, this.controller.camSettings.near); // R = r + u + d * nearPlane
		normalise3(dir);

		return [
			this.controller.position,
			dir
		];
	}

	private tmp = [
		new Float32Array(3),
		new Float32Array(3)
	];

	// Transform a ray from world space into the local space of the BVH and test for intersection
	public testRayBVH (origin: Float32Array, dir: Float32Array, worldToObject: THREE.Matrix4, bvh: BVH): [boolean, number[]] {
		const [O, d] = this.tmp;
		O.set(origin); d.set(dir);
		applyMatrix4(worldToObject, O); transformDirection(worldToObject, d);
		return [bvh.testRayRoot(O, d), bvh.testRayLeaf(O, d)];
	}

}
