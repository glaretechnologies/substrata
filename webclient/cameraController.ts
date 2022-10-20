/*=====================================================================
cameraController.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

/*
A class to wrap the camera functions and handle 1st and 3rd person views.  It also holds the THREE.PerspectiveCamera
*/

import * as THREE from './build/three.module.js';
import Caster from './physics/caster.js';
import { clamp } from './maths/functions.js';
import { add3 } from './maths/vec3.js';

export const DEFAULT_FOV = 75;
export const DEFAULT_NEAR = 0.1;
export const DEFAULT_FAR = 1000.0;
const ROT_FACTOR = 3e-3;
const DEFAULT_CAM_DIST = 3.0;

const HEADING = 1;
const PITCH = 2;

export enum CameraMode {
  FIRST_PERSON,
  THIRD_PERSON
}

// The AR can be safely calculated from the window dimensions
export function getAR (): number {
	return window.innerWidth / window.innerHeight;
}

// The controller basically holds all the relevant camera state and provides functions for updating the camera.  It also
// holds references to ray casters, the renderer, the frustum, etc.
export default class CameraController {
	public readonly camera: THREE.PerspectiveCamera;
	private readonly rndr_: THREE.WebGLRenderer;
	private readonly caster_: Caster;

	private readonly position_: Float32Array;
	private readonly rotation_: Float32Array;

	private readonly camForwardsVec_: Float32Array;
	private readonly camRightVec_: Float32Array;
	private readonly positionV3_: THREE.Vector3;

	private readonly camPos3rdPerson: Float32Array;
	private readonly camDelta: Float32Array;

	private camDistance_ = DEFAULT_CAM_DIST;

	public readonly camSettings = {
		near: DEFAULT_NEAR,
		far: DEFAULT_FAR,
		fov: DEFAULT_FOV
	};

	private mode_: CameraMode = CameraMode.FIRST_PERSON;

	public constructor (renderer: THREE.WebGLRenderer) {
		this.rndr_ = renderer; // Needed for getSize which takes dPR into account
		this.camera = new THREE.PerspectiveCamera(DEFAULT_FOV, getAR(), DEFAULT_NEAR, DEFAULT_FAR);
		this.camera.updateProjectionMatrix();
		window.addEventListener('resize', this.onResize);
		this.onResize();

		this.caster_ = new Caster(renderer, this);

		this.position_ = new Float32Array(3);
		this.rotation_ = new Float32Array([0, Math.PI / 2, Math.PI / 2]);
		this.camForwardsVec_ = new Float32Array(3);
		this.camRightVec_ = new Float32Array(3);

		this.camera.position.set(0, 0, 0);
		this.camera.up = new THREE.Vector3(0, 0, 1);
		this.camera.lookAt(new THREE.Vector3(0, 1, 0)); // We are positioned at [0, 0, 0]
		this.positionV3_ = new THREE.Vector3;

		// For 3rd person camera
		this.camPos3rdPerson = new Float32Array([0, -1, 1]);
		this.camDelta = new Float32Array(3);
	}

	private onResize = () => {
		this.camera.aspect = getAR();
		this.camera.updateProjectionMatrix();
	};

	// Always update the cached value on call
	public get camForwardsVec (): Float32Array {
		const sp = Math.sin(this.rotation_[PITCH]);
		this.camForwardsVec_[0] = Math.cos(this.rotation_[HEADING]) * sp;
		this.camForwardsVec_[1] = Math.sin(this.rotation_[HEADING]) * sp;
		this.camForwardsVec_[2] = Math.cos(this.rotation_[PITCH]);
		return this.camForwardsVec_;
	}

	public get camRightVec (): Float32Array {
		this.camRightVec_[0] = Math.sin(this.rotation_[HEADING]);
		this.camRightVec_[1] = -Math.cos(this.rotation_[HEADING]);
		return this.camRightVec_;
	}

	public get caster (): Caster { return this.caster_; }

	public handleScroll(delta: number): void {
		const dist = this.camDistance_;
		this.camDistance_ = clamp(dist - (dist * delta * 2e-3), 0.5, 20.0);
	}

	public get camDistance (): number { return this.camDistance_; }
	public set camDistance (dist: number) { this.camDistance_ = clamp(dist, 0.5, 20.0); }

	public get isFirstPerson (): boolean { return this.mode_ === CameraMode.FIRST_PERSON; }
	public get isThirdPerson (): boolean { return this.mode_ === CameraMode.THIRD_PERSON; }

	public get cameraMode (): CameraMode { return this.mode_; }
	public set cameraMode (mode: CameraMode) { this.mode_ = mode; }

	public get positionV3(): THREE.Vector3 {
		return this.mode_ === CameraMode.FIRST_PERSON ? this.camera.position : this.positionV3_;
	}

	// These functions replicate the interface in substrata
	public get firstPersonPos (): Float32Array { return this.position_; }
	public get thirdPersonPos (): Float32Array { return this.camPos3rdPerson; }
	public set thirdPersonPos (pos: Float32Array) {
		this.camPos3rdPerson.set(pos);
		if(this.isThirdPerson) {
			this.camera.position.set(...pos);
		}
	}

	public get position (): Float32Array {
		return this.mode_ === CameraMode.FIRST_PERSON ? this.position_ : this.camPos3rdPerson;
	}

	public set position (pos: Float32Array) {
		this.position_.set(pos);
		this.positionV3_.set(...pos);
		this.camera.position.set(...pos);
		if(this.isFirstPerson) {
			this.camera.position.set(...pos);
		}
	}

	public get rotation(): Float32Array { return this.rotation_; }
	public set rotation (rot: Float32Array) { this.rotation_.set(rot); }

	public get pitch (): number { return this.rotation_[PITCH]; }
	public set pitch (value: number) { this.rotation_[PITCH] = value; }
	public get heading (): number { return this.rotation_[HEADING]; }
	public set heading (value: number) { this.rotation_[HEADING] = value; }

	// Not sure how this should work in 3rd person view just yet...
	public mouseLook (moveX: number, moveY: number): void {
		this.rotation_[HEADING] += -moveX * ROT_FACTOR;
		this.rotation_[PITCH] = clamp(this.rotation_[PITCH] + moveY * ROT_FACTOR, 1e-3, Math.PI - 1e-3);
		this.updateView();
	}

	private tmp = new Float32Array(3);
	public updateView (): void {
		this.camera.lookAt(...add3(this.position, this.camForwardsVec, this.tmp));
		this.camera.updateMatrix();
	}
}
