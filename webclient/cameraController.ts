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

export const HORIZONTAL_FOV = 73.821909132; // = 2 * asin((0.035/2) / 0.025)  (To match native calcs in GlWidget, see sensorWidth() etc.)
export const DEFAULT_NEAR = 0.1;
export const DEFAULT_FAR = 1000.0;
export const MAX_CAM_DIST = 20.0;
const ROT_FACTOR = 3e-3;
const DEFAULT_CAM_DIST = 3.0;

// const SPEED_BASE = (1. + Math.sqrt(5)) * .5;
// const CAP = 1e-4;

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

	private readonly camForwardsVec_: Float32Array; // in z-up coords
	private readonly camRightVec_: Float32Array; // in z-up coords
	private readonly positionV3_: THREE.Vector3; // in z-up coords

	private readonly camPos3rdPerson: Float32Array;
	private readonly camDelta: Float32Array;

	private temp_pos_z_up: THREE.Vector3;

	/*
	public invertSidewaysMovement: boolean;
	public invertMouse: boolean;
	private mouseSensitivityScale_: number;
	private moveSpeedScale_: number;
	*/

	private camDistance_ = DEFAULT_CAM_DIST;

	public readonly camSettings = {
		near: DEFAULT_NEAR,
		far: DEFAULT_FAR,
		horizontal_fov: HORIZONTAL_FOV
	};

	private mode_: CameraMode = CameraMode.FIRST_PERSON;

	public constructor (renderer: THREE.WebGLRenderer) {
		this.rndr_ = renderer; // Needed for getSize which takes dPR into account
		this.camera = new THREE.PerspectiveCamera(HORIZONTAL_FOV / getAR(), getAR(), DEFAULT_NEAR, DEFAULT_FAR);
		this.camera.updateProjectionMatrix();
		window.addEventListener('resize', this.onResize);
		this.onResize();

		this.caster_ = new Caster(renderer, this);

		this.position_ = new Float32Array(3);
		this.rotation_ = new Float32Array([0, Math.PI / 2, Math.PI / 2]);
		this.camForwardsVec_ = new Float32Array(3);
		this.camRightVec_ = new Float32Array(3);

		this.camera.position.set(0, 0, 0);
		this.camera.up = new THREE.Vector3(0, 1, 0);
		this.camera.lookAt(new THREE.Vector3(0, 0, -1)); // We are positioned at [0, 0, 0]
		this.positionV3_ = new THREE.Vector3;

		// For 3rd person camera
		this.camPos3rdPerson = new Float32Array([0, -1, 1]);
		this.camDelta = new Float32Array(3);

		this.temp_pos_z_up = new THREE.Vector3(0, 0, 0);

		/*
		this.moveSpeedScale_ = 1;
		this.mouseSensitivityScale_ = 1;
		this.invertSidewaysMovement = false;
		this.invertMouse = false;
 	  */
	}

	private onResize = () => {
		this.camera.fov = HORIZONTAL_FOV / getAR();
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
		this.camDistance_ = clamp(dist - (dist * delta * 2e-3), 0.5, MAX_CAM_DIST);
	}

	public get camDistance (): number { return this.camDistance_; }
	public set camDistance (dist: number) { this.camDistance_ = clamp(dist, 0.5, MAX_CAM_DIST); }

	public get isFirstPerson (): boolean { return this.mode_ === CameraMode.FIRST_PERSON; }
	public get isThirdPerson (): boolean { return this.mode_ === CameraMode.THIRD_PERSON; }

	public get cameraMode (): CameraMode { return this.mode_; }
	public set cameraMode (mode: CameraMode) { this.mode_ = mode; }

	public get positionV3(): THREE.Vector3 {
		this.temp_pos_z_up.set(this.camera.position.x, -this.camera.position.z, this.camera.position.y); // Convert from y-up to z-up
		return this.mode_ === CameraMode.FIRST_PERSON ? this.temp_pos_z_up : this.positionV3_;
	}

	// These functions replicate the interface in substrata
	public get firstPersonPos (): Float32Array { return this.position_; }
	public get thirdPersonPos (): Float32Array { return this.camPos3rdPerson; }
	public set thirdPersonPos (pos: Float32Array) {
		this.camPos3rdPerson.set(pos);
		if(this.isThirdPerson) {
			this.camera.position.set(pos[0], pos[2], -pos[1]); // // Convert to y-up
		}
	}

	public get position (): Float32Array {
		return this.mode_ === CameraMode.FIRST_PERSON ? this.position_ : this.camPos3rdPerson;
	}

	public set position (pos: Float32Array) {
		this.position_.set(pos);
		this.positionV3_.set(...pos);
		this.camera.position.set(pos[0], pos[2], -pos[1]); // Convert to y-up
		if(this.isFirstPerson) {
			this.camera.position.set(pos[0], pos[2], -pos[1]); // Convert to y-up
		}
	}

	public get rotation(): Float32Array { return this.rotation_; }
	public set rotation (rot: Float32Array) { this.rotation_.set(rot); }

	public get pitch (): number { return this.rotation_[PITCH]; }
	public set pitch (value: number) { this.rotation_[PITCH] = value; }
	public get heading (): number { return this.rotation_[HEADING]; }
	public set heading (value: number) {
		this.rotation_[HEADING] = value;
		const pos = new Float32Array(this.position);
		add3(pos, this.camForwardsVec);
		this.camera.lookAt(pos[0], pos[2], -pos[1]); // Convert to y-up
	}

	public mouseLook (moveX: number, moveY: number): void {
		this.rotation_[HEADING] += -moveX * ROT_FACTOR;
		this.rotation_[PITCH] = clamp(this.rotation_[PITCH] + moveY * ROT_FACTOR, 1e-3, Math.PI - 1e-3);
		this.updateView();
	}

	private tmp = new Float32Array(3);
	public updateView(): void {
		add3(this.position, this.camForwardsVec, this.tmp)
		this.camera.lookAt(this.tmp[0], this.tmp[2], -this.tmp[1]); // Convert to y-up
		this.camera.updateMatrix();
	}

	/*
  public get mouseSensitivityScale (): number { return this.mouseSensitivityScale_; }
	public set mouseSensitivityScale (value: number) { this.mouseSensitivityScale_ = Math.pow(SPEED_BASE, value); }

	public get moveSpeedScale (): number { return this.moveSpeedScale_; }
	public set moveSpeedScale (value: number) { this.moveSpeedScale_ = value; }

	public getUpForForwards (dir: Float32Array): Float32Array {
		const up = new Float32Array(UP_VECTOR);
		if(Math.abs(dot3(dir, up))) {
			return up;
		}	else {
			removeComponentInDir(up, dir);
			return normalise3(up);
		}
	}

	public update (posDelta: Float32Array, rotDelta: Float32Array) {
		// These two values are used in CameraController::update() and not updated anywhere that I can tell
		const baseMoveSpeed = .035;
		const baseRotateSpeed = .005;

		const rotSpeed = baseRotateSpeed * this.mouseSensitivityScale_;
		const moveSpeed = baseMoveSpeed * this.moveSpeedScale_;
		const sideDirFactor = this.invertSidewaysMovement ? -1. : 1;

		if(!eq(rotDelta[0], 0.) || !eq(rotDelta[1], 0.)) {
			this.rotation_[0] += rotDelta[1] * -rotSpeed;
			this.rotation_[1] += rotDelta[0] * -rotSpeed * (this.invertMouse ? -1 : 1);
			this.rotation_[1] = Math.max(CAP, Math.min(PI - CAP, this.rotation_[1]));
		}

		const forwards = new Float32Array([
			Math.sin(this.rotation_[1]) * Math.cos(this.rotation_[0]),
			Math.sin(this.rotation_[1]) * Math.sin(this.rotation_[0]),
			Math.cos(this.rotation_[1])
		]);

		const up = this.getUpForForwards(forwards);
		const right = cross3(forwards, up, new Float32Array(3);
	}
	*/
}
