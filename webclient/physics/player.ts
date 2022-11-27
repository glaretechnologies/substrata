/*=====================================================================
player.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// Modelled on the PlayerPhysics class in Substrata

import * as THREE from '../build/three.module.js';
import PhysicsWorld from './world.js';
import { DIST, makeSphereTraceResult, NOR_X, POS_Z } from './types.js';
import {
	add3,
	addScaled3,
	eq3,
	len3,
	mulScalar3,
	normalise3,
	removeComponentInDir,
	sqLen3,
	sub3
} from '../maths/vec3.js';
import CameraController, { CameraMode } from '../cameraController.js';

export const SPHERE_RAD = 0.3;
const REPEL_RADIUS = SPHERE_RAD + 0.005;
export const EYE_HEIGHT = 1.67;
export const GRAVITY = new Float32Array([0, 0, -9.81]);
export const UP_VECTOR = new Float32Array([0, 0, 1]);
export const ZERO = new Float32Array(3);

const RUN_FACTOR = 5;
const MOVE_SPEED = 3;
const JUMP_SPEED = 4.5;
const MAX_AIR_SPEED = 8;

const JUMP_PERIOD = 0.1; // Allow a jump command to be executed even if the player is not quite on the ground yet.

export interface SpringSphereSet {
  collisionPoints: Float32Array[] // For now, an array of points, move to contiguous, dynamic array
  sphere: Float32Array // The bounding sphere [ cx, cy, cz, radius ]
}

export function makeSpringSphereSet (): SpringSphereSet {
	return {
		collisionPoints: [],
		sphere: new Float32Array(4)
	};
}

export class PlayerPhysics {
	private cameraController: CameraController | undefined;

	private world_: PhysicsWorld;
	private visGroup_: THREE.Group | undefined;

	private readonly velocity_: Float32Array;
	private readonly moveImpulse_: Float32Array;
	private readonly lastGroundNormal_: Float32Array;
	private readonly lastPos_: Float32Array;

	private lastChange: number;

	private jumpTimeRemaining_: number;
	private onGround_: boolean;
	private lastRunPressed_: boolean;
	private flyMode_: boolean;
	private timeSinceOnGround_: number;

	private readonly springSphereSet: SpringSphereSet[];

	// This is the amount which the displayed camera position is below the actual physical avatar position.
	// This is to allow the physical avatar position to step up discontinuously, whereas the camera position will
	// smoothly increase to match the physical avatar position.
	private camPosZDelta_: number;

	public constructor (world: PhysicsWorld) {
		this.world_ = world;
		this.velocity_ = new Float32Array(3);
		this.moveImpulse_ = new Float32Array(3);
		this.lastGroundNormal_ = new Float32Array(UP_VECTOR);
		this.lastPos_ = new Float32Array(3);

		this.jumpTimeRemaining_ = 0;
		this.onGround_ = true;
		this.lastRunPressed_ = false;
		this.flyMode_ = false;
		this.timeSinceOnGround_ = 0;

		this.camPosZDelta_ = 0;

		this.lastChange = 0; // A timer for debouncing the camera mode

		// Used for one each of the body spheres
		this.springSphereSet = [
			makeSpringSphereSet(),
			makeSpringSphereSet(),
			makeSpringSphereSet()
		];
	}

	public get controller (): CameraController { return this.cameraController; }
	public set controller (controller: CameraController) {
		this.cameraController = controller;
	}

	public get flyMode (): boolean { return this.flyMode_; }
	public set flyMode (value: boolean) { this.flyMode_ = value; }

	public processMoveForwards (factor: number, runPressed: boolean): void {
		const speed = runPressed ? MOVE_SPEED * RUN_FACTOR * factor : MOVE_SPEED * factor;
		addScaled3(this.moveImpulse_, this.cameraController.camForwardsVec, speed);
	}

	public processMoveRight (factor: number, runPressed: boolean): void {
		const speed = runPressed ? MOVE_SPEED * RUN_FACTOR * factor : MOVE_SPEED * factor;
		addScaled3(this.moveImpulse_, this.cameraController.camRightVec, speed);
	}

	public processMoveUp (factor: number, runPressed: boolean): void {
		if(this.flyMode_) {
			const speed = runPressed ? MOVE_SPEED * RUN_FACTOR * factor : MOVE_SPEED * factor;
			addScaled3(this.moveImpulse_, UP_VECTOR, speed);
		}
	}

	public processJump (): void {
		this.jumpTimeRemaining_ = JUMP_PERIOD;
	}

	public get lastPosition (): Float32Array { return this.lastPos_; }

	// Code ported from Substrata PlayerPhysics::update
	public update (dt: number, camPosOut: Float32Array): boolean { // Returns if the player jumped or not
		let jumped = false;
		dt = Math.min(dt, 0.1);

		if(this.jumpTimeRemaining_ > 0) {
			if(this.onGround_) {
				this.onGround_ = false;
				jumped = true;
				addScaled3(this.velocity_, UP_VECTOR, JUMP_SPEED);
				this.timeSinceOnGround_ = 1;
			}
		}

		this.jumpTimeRemaining_ -= dt;

		if(!this.flyMode_) {
			if(this.onGround_) {
				const parallel_impulse = new Float32Array(this.moveImpulse_);
				removeComponentInDir(this.lastGroundNormal_, parallel_impulse);
				this.velocity_.set(parallel_impulse);
			}

			const dvel = new Float32Array(GRAVITY);

			if(!this.onGround_) {
				const horz_impulse = new Float32Array(this.moveImpulse_);
				horz_impulse[2] = 0;
				add3(dvel, horz_impulse); // dvel += horz_impulse

				const vel2Len = Math.sqrt(dvel[0]*dvel[0] + dvel[1]*dvel[1]);
				if(vel2Len > MAX_AIR_SPEED) {
					const s = MAX_AIR_SPEED/vel2Len;
					dvel[0] *= s; dvel[1] *= s;
				}

				addScaled3(this.velocity_, dvel, dt); // this.velocity_ += dvel * dt

				if(this.velocity_[2] < -100) this.velocity_[2] = -100;
			}
		} else { // this.flyMode_ === true
			const speed = len3(this.velocity_);
			const desired_vel = new Float32Array(3);
			const len = len3(this.moveImpulse_);
			if(len > 1.e-4) {
				addScaled3(desired_vel, this.moveImpulse_, speed/len); // desired_vel = normalise(this.moveImpulse_) * speed
			}

			const accel = new Float32Array(this.moveImpulse_); // accel = this.moveImpulse_
			mulScalar3(accel, 3.0); // accel *= 3.0
			const delta = sub3(desired_vel, this.velocity_, new Float32Array(3)); // delta = desired_vel - this.velocity_
			addScaled3(accel, delta, 2.0); // accel += delta * 2.0
			addScaled3(this.velocity_, accel, dt); // this.velocity_ += accel * dt
		}

		this.onGround_ = false;

		const dpos = new Float32Array(this.velocity_); // dpos = this.velocity_
		mulScalar3(dpos, dt); // dpos *= dt
		const camPos = new Float32Array(camPosOut);

		camPos[2] += this.camPosZDelta_;
		this.camPosZDelta_ = Math.max(0, this.camPosZDelta_ - 20 * dt * this.camPosZDelta_);

		for(let i = 0; i !== 5; ++i) {
			if(!eq3(dpos, ZERO)) {
				const closestResult = makeSphereTraceResult();
				closestResult.data[DIST] = 1e9;
				let hitSomething = false;

				for(let s = 0; s !== 3; ++s) {
					const spherePos = new Float32Array(camPos);
					spherePos[2] = spherePos[2] - EYE_HEIGHT + SPHERE_RAD * (5 - 2 * s);
					const playersphere = new Float32Array(4);
					playersphere.set(spherePos); playersphere[3] = SPHERE_RAD;
					const result = this.world_.traceSphereWorld(playersphere, dpos);
					if(result.hit) {
						const dist = result.data[DIST];
						if(dist !== -1 && dist < closestResult.data[DIST]) {
							hitSomething = true;
							closestResult.data.set(result.data);
							closestResult.pointInTri = result.pointInTri;
						}
					}
				}

				if(hitSomething) {
					const usefraction = closestResult.data[DIST] / len3(dpos);

					addScaled3(camPos, dpos, usefraction); // camPos += dpos * usefraction
					mulScalar3(dpos, 1.0 - usefraction); // dpos *= 1.0 - usefraction

					//---------------------------------- Do stair climbing ----------------------------------
					const foot_z = camPos[2] - EYE_HEIGHT;
					const hitpos_height_above_foot = closestResult.data[POS_Z] - foot_z;

					if(!closestResult.pointInTri && hitpos_height_above_foot > 3e-3 && hitpos_height_above_foot < .25) {
						const jump_up_amount = hitpos_height_above_foot + .01;
						const spherePos = new Float32Array([camPos[0], camPos[1], camPos[2] - EYE_HEIGHT + SPHERE_RAD * 5]); // Upper sphere centre
						const playersphere = new Float32Array(4);
						playersphere.set(spherePos); playersphere[3] = SPHERE_RAD;
						const result = this.world_.traceSphereWorld(playersphere, new Float32Array([0, 0, jump_up_amount]));

						if(result.hit == null) {
							camPos[2] += jump_up_amount;
							this.camPosZDelta_ = Math.min(0.3, this.camPosZDelta_ + jump_up_amount);
							closestResult.data.set(UP_VECTOR, NOR_X); // Set the closestResult.hit_normal to [0, 0, 1]
						}
					}
					//---------------------------------- End stair climbing ----------------------------------

					const was_just_falling = this.velocity_[0] === 0 && this.velocity_[1] === 0;
					const hit_normal = closestResult.data.slice(NOR_X, NOR_X+3);
					removeComponentInDir(hit_normal, dpos); // remove hit_normal from the delta position
					removeComponentInDir(hit_normal, this.velocity_); // remove hit_normal from the velocity

					if(hit_normal[2] > 0.5) {
						this.onGround_ = true;
						this.lastGroundNormal_.set(hit_normal);

						if(was_just_falling) dpos.fill(0);
					}
				} else { // Did not hit anything
					add3(camPos, dpos); // camPos += dpos
					dpos.fill(0); // dpos = [0, 0, 0]
				}

				const set = this.springSphereSet;
				for(let s = 0; s !== 3; ++s) {
					const spherepos = new Float32Array(camPos);
					spherepos[2] = spherepos[2] - EYE_HEIGHT + 1.5 - s * 0.6;
					const bigsphere = new Float32Array(4);
					bigsphere.set(spherepos); bigsphere[3] = REPEL_RADIUS;
					set[s].sphere.set(bigsphere);
					this.world_.getCollPoints(bigsphere, set[s].collisionPoints);
				}

				let displacement = this.doSpringRelaxation(set, false, true);
				if(!eq3(displacement, ZERO) && displacement[2]/len3(displacement) > .5) this.onGround_ = true;

				displacement = this.doSpringRelaxation(set, this.onGround_ && eq3(this.moveImpulse_, ZERO), false);
				add3(camPos, displacement); // camPos += displacement
			}
		} // i !== 5 loop

		if(!this.onGround_) this.timeSinceOnGround_ += dt;
		else this.timeSinceOnGround_ = 0;

		camPosOut.set(camPos);
		camPosOut[2] -= this.camPosZDelta_;

		this.moveImpulse_.fill(0);
		return jumped;
	}

	public get cameraMode (): CameraMode { return this.cameraController.cameraMode ?? CameraMode.FIRST_PERSON;}
	public set cameraMode (mode: CameraMode) {
		this.controller.cameraMode = mode;
		this.visGroup_.visible = mode === CameraMode.THIRD_PERSON;
	}

	public processCameraMovement(dt: number) {
		const [pos] = this.tmp;
		const controller = this.cameraController;

		pos.set(controller.firstPersonPos);
		this.update(dt, pos);
		controller.position = pos;
		this.world_.ground.updateGroundPlane(pos);
		if(this.visGroup_) this.visGroup_.position.set(pos[0], pos[2] - EYE_HEIGHT, -pos[1]);  // Convert to y-up

		if(controller.isThirdPerson) {
			const target = controller.firstPersonPos;
			const back = mulScalar3(controller.camForwardsVec, -controller.camDistance, new Float32Array(3));
			addScaled3(back, UP_VECTOR, 0.2);
			const d = normalise3(back, new Float32Array(3));

			const query = this.world_.traceRay(target, d);
			if(query !== -1 && query < controller.camDistance) {
				addScaled3(target, d, query-.001, back);
			} else {
				add3(back, target);
			}

			controller.thirdPersonPos = back;
		}
	}

	// Returns displacement
	public doSpringRelaxation (sphereSet: SpringSphereSet[], constrainToVertical: boolean, fastMode: boolean): Float32Array {
		const displacement = new Float32Array(3);
		const force = new Float32Array(3);
		const currentspherepos = new Float32Array(3);
		const springvec = new Float32Array(3);
		const scaled = new Float32Array(3);

		const EPSILON_SQ = 1e-8;

		const max_iters = fastMode ? 1 : 100;
		for(let i = 0; i !== max_iters; ++i) {
			force.fill(0);
			let numforces = 0;
			for(let s = 0; s !== sphereSet.length; ++s) {
				const ss = sphereSet[s];
				add3(ss.sphere, displacement, currentspherepos); // currentspherepos = sphere.centre + displacement

				for(let c = 0; c !== ss.collisionPoints.length; ++c) {
					const col = ss.collisionPoints[c];
					sub3(currentspherepos, col, springvec); // springvec = currentspherepos - collisionPoint
					const springLen = len3(springvec);
					mulScalar3(springvec, 1./springLen); // normalise springvec
					if(springLen < ss.sphere[3]) { // Check against sphere radius
						addScaled3(force, springvec, ss.sphere[3] - springLen); // force += springvec * (radius - springlen)
						numforces += 1;
					}
				}
			}

			if(numforces !== 0) mulScalar3(force, 1./numforces);    // force *= 1./numforces
			if(constrainToVertical) force[0] = force[1] = 0;
			if(sqLen3(force) < EPSILON_SQ) break;

			mulScalar3(force, 0.3, scaled);                         // scaled = force * 0.3
			add3(displacement, scaled);                                // displacement += scaled
		}

		return displacement;
	}

	// Packs 3 bounding spheres into the spheres array of [ s0x, s0y, s0z, s0radius, s1x, s1y, etc. ]
	public getCollisionSpheres(pos: Float32Array, spheres: Float32Array) {
		const [delta] = this.tmp;
		delta.fill(0);

		for(let i = 0; i !== 3; ++i) {
			delta[2] = i * (2 * SPHERE_RAD) - EYE_HEIGHT;
			add3(pos, delta, spheres.subarray(4*i, 4*i+3));
			spheres[4*i+3] = SPHERE_RAD;
		}
	}

	// Add the player spheres to the passed group
	public addAvatarBounds (group: THREE.Group): void {
		if(this.visGroup_) return;

		this.visGroup_ = new THREE.Group();

		const geo = new THREE.SphereGeometry(); // Scale in mesh
		const mat = new THREE.MeshStandardMaterial({
			color: new THREE.Color(.972, .960, .915),
			transparent: true,
			opacity: 0.4,
			metalness: 0.8,
			roughness: .1,
		});
		for(let s = 0; s !== 3; ++s) {
			const z = 1.5 - s * 0.6;
			const mesh = new THREE.Mesh(geo, mat);
			mesh.position.set(0, z, 0); // Convert to y-up
			mesh.scale.set(SPHERE_RAD, SPHERE_RAD, SPHERE_RAD);
			this.visGroup_.add(mesh);
		}

		// We start in first person mode
		this.visGroup_.visible = false;

		group.add(this.visGroup_);
	}

	private tmp = [
		new Float32Array(3)
	];
}