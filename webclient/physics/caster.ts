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
import { add3, addScaled3, cross3, mulScalar3, normalise3 } from '../maths/vec3.js';
import { DEG_TO_RAD } from '../maths/defs.js';

const BOTTOM = 0;
const TOP = 1;

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

		this.theta = .5 * controller.camSettings.fov * DEG_TO_RAD;
		this.htan = Math.tan(this.theta);
		this.frustum[TOP] = controller.camSettings.near * this.htan;
		this.frustum[BOTTOM] = - this.frustum[TOP];
	}

	// Calculate a pick ray based on the current camera view at screen coordinates [x, y]
	public getPickRay (x: number, y: number): [THREE.Vector3, THREE.Vector3] | null { // [ Origin, Dir ]
		const dPR = this.rndr.getPixelRatio();
		this.rndr.getSize(this.dims);
		if(x < 0 || x > this.dims.x || y < 0 || y > this.dims.y) return null;

		const invH = 1. / this.dims.y;
		const AR = this.dims.x * invH;
		const r = dPR * x / this.dims.x, u = (this.dims.y - dPR * y) * invH;
		const right = AR * this.frustum[TOP]; const left = -right;

		const U = this.U;

		const R = new Float32Array(this.controller.camRightVec);
		const D = new Float32Array(this.controller.camForwardsVec);

		cross3(D, R, U); // U = D x R
		mulScalar3(R, lerpN(left, right, r)); // R *= lerp(left, right)
		mulScalar3(U, lerpN(this.frustum[TOP], this.frustum[BOTTOM], u)); // U *= lerp(top, bottom)

		const dir = addScaled3(add3(R, U), D, this.controller.camSettings.near); // R = r + u + d * nearPlane
		normalise3(dir);

		return [
			this.controller.positionV3,
			new THREE.Vector3(...dir)
		];
	}

	private tmp = [
		new THREE.Vector3(),
		new THREE.Vector3(),
		new Float32Array(3),
		new Float32Array(3)
	];

	// Transform a ray from world space into the local space of the BVH and test for intersection
	public testRayBVH (origin: THREE.Vector3, dir: THREE.Vector3, worldToObject: THREE.Matrix4, bvh: BVH): [boolean, number[]] {
		const [O, d, Of, df] = this.tmp;
		O.copy(origin); d.copy(dir);
		O.applyMatrix4(worldToObject); d.transformDirection(worldToObject);
		Of[0] = O.x; Of[1] = O.y; Of[2] = O.z;
		df[0] = d.x; df[1] = d.y; df[2] = d.z;

		return [bvh.testRayRoot(Of, df), bvh.testRayLeaf(Of, df)];
	}
}
