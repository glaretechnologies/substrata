// Modelled on the PlayerPhysics class in Substrata

import * as THREE from '../build/three.module.js';
import PhysicsWorld from './world.js';
import { DIST, makeSphereTraceResult, NOR_X, POS_Z } from './types.js';
import {
  add3,
  addScaled3,
  eq3,
  fromVector3,
  len3,
  mulScalar3,
  removeComponentInDir,
  sqLen3,
  sub3
} from '../maths/vec3.js';
import { print3 } from '../maths/functions.js';
import { WorldObject } from '../webclient.js';

export const SPHERE_RAD = 0.3;
const REPEL_RADIUS = SPHERE_RAD + 0.005;
export const EYE_HEIGHT = 1.67;
export const GRAVITY = new Float32Array([0, 0, -0.981]);
//export const GRAVITY = new Float32Array(3);
export const UP_VECTOR = new Float32Array([0, 0, 1]);
export const ZERO = new Float32Array(3);

const RUN_FACTOR = 5;
const MOVE_SPEED = 3;
const JUMP_SPEED = 4.5;
const MAX_AIR_SPEED = 8;

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
  private keyState_: Set<string> | undefined;
  private camera_: THREE.PerspectiveCamera | undefined;

  private world_: PhysicsWorld | undefined;

  private readonly velocity_: Float32Array;
  private readonly moveImpulse_: Float32Array;
  private readonly lastGroundNormal_: Float32Array;
  private readonly lastPos_: Float32Array;

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

  private camForwardsVec_: () => THREE.Vector3 | undefined;
  private camRightVec_: () => THREE.Vector3 | undefined;

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

    // Used for one each of the body spheres
    this.springSphereSet = [
      makeSpringSphereSet(),
      makeSpringSphereSet(),
      makeSpringSphereSet()
    ];
  }

  // TODO: This is a temporary interface (REMOVE ME BEFORE CHECKIN)
  public set camForwardsVec (cb: () => THREE.Vector3) { this.camForwardsVec_ = cb; }
  public set camRightVec (cb: () => THREE.Vector3) { this.camRightVec_ = cb; }

  public get camera (): THREE.PerspectiveCamera { return this.camera_; }
  public set camera (cam: THREE.PerspectiveCamera) {
    this.camera_ = cam;
  }

  public get flyMode (): boolean { return this.flyMode_; }
  public set flyMode (value: boolean) { this.flyMode_ = value; }

  public get keyState (): Set<string> { return this.keyState_; }
  public set keyState (set: Set<string>) { this.keyState_ = set; }

  public get lastPosition (): Float32Array { return this.lastPos_; }

  // Note: Removed UpdateEvents return as it wasn't used in Substrata
  // Code ported from Substrata PlayerPhysics::update
  public update (dt: number, camPosOut: Float32Array): void {
    dt = Math.min(dt, 0.1);

    if(this.jumpTimeRemaining_ > 0) {
      if(this.onGround_) {
        this.onGround_ = false;
        console.log('jumping.');
        addScaled3(this.velocity_, UP_VECTOR, JUMP_SPEED);
        this.timeSinceOnGround_ = 1;
      }
    }

    this.jumpTimeRemaining_ -= dt;

    if(!this.flyMode_) {
      //console.log('onGround:', this.onGround_);
      if(this.onGround_) {
        const parallel_impulse = new Float32Array(this.moveImpulse_);
        removeComponentInDir(this.lastGroundNormal_, parallel_impulse);
        // console.log('lastGroundNormal:', this.lastGroundNormal_);
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

    //print3('camPosOut', camPosOut, this.camPosZDelta_);

    camPos[2] += this.camPosZDelta_;
    this.camPosZDelta_ = Math.max(0, this.camPosZDelta_ - 20 * dt * this.camPosZDelta_);


    for(let i = 0; i !== 5; ++i) {
      if(!eq3(dpos, ZERO)) {
        const closestResult = makeSphereTraceResult();
        closestResult.data[DIST] = 1e9;
        let hitSomething = false;

        for(let s = 0; s !== 3; ++s) { // for each sphere in the body
          const spherePos = new Float32Array(camPos);
          spherePos[2] -= EYE_HEIGHT + SPHERE_RAD * (5 - 2 * s);
          const playersphere = new Float32Array([...camPos, SPHERE_RAD]);
          const result = this.world_.traceSphereWorld(playersphere, dpos);
          if(result.hit) {
            const dist = result.data[DIST];
            if(dist !== -1 && dist < closestResult.data[DIST]) {
              console.log('sphere:', s, spherePos[2], camPos[2]);
              hitSomething = true;
              closestResult.data.set(result.data);
              closestResult.pointInTri = result.pointInTri;
            }
          }
        }

        /* Virtual Ground Plane
        if(!hitSomething && camPos[2] < EYE_HEIGHT) {
          closestResult.data.set([camPos[0], camPos[1], camPosOut[2] - EYE_HEIGHT, 0, 0, 1, camPosOut[2] - EYE_HEIGHT]);
          closestResult.pointInTri = false;
          hitSomething = true;
        }
        */

        if(hitSomething) {
          console.log('hit:', closestResult.data[DIST]);

          const usefraction = closestResult.data[DIST] / len3(dpos);
          if(usefraction < 0 || usefraction > 1) console.warn('Failed assert usefraction'); // TODO: REMOVE

          addScaled3(camPos, dpos, usefraction); // camPos += dpos * usefraction
          mulScalar3(dpos, 1.0 - usefraction); // dpos *= 1.0 - usefraction

          const foot_z = camPos[2] - EYE_HEIGHT;
          const hitpos_height_above_foot = closestResult.data[POS_Z] - foot_z;
          print3('foot_z', foot_z, 'hitpos', hitpos_height_above_foot, 'pnt:', closestResult.data.slice(0, 3));

          if(!closestResult.pointInTri && hitpos_height_above_foot > 3e-3 && hitpos_height_above_foot < .25) {
            const jump_up_amount = hitpos_height_above_foot + .01;
            const spherePos = new Float32Array([camPos[0], camPos[1], camPos[2] - EYE_HEIGHT + SPHERE_RAD * 5]);
            //print3('spherePos', spherePos);
            const playersphere = new Float32Array([...spherePos, SPHERE_RAD]);
            const result = this.world_.traceSphereWorld(playersphere, new Float32Array([0, 0, jump_up_amount]));

            //console.log('result:', result);
            if(!result.hit) {
              console.log('!result.hit');
              camPos[2] += jump_up_amount;
              this.camPosZDelta_ = Math.min(0.3, this.camPosZDelta_ + jump_up_amount);
              closestResult.data.set(UP_VECTOR, NOR_X); // Set the closestResult.hit_normal to [0, 0, 1]
            }
          }

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
          //print3('dpos', dpos);
          dpos.fill(0); // dpos = [0, 0, 0]
        }

        const set = this.springSphereSet;
        for(let s = 0; s !== 3; ++s) {
          const spherepos = new Float32Array(camPos);
          spherepos[2] -= EYE_HEIGHT - 1.5 + s * 0.6;
          const bigsphere = new Float32Array([...spherepos, REPEL_RADIUS]);
          set[s].sphere.set(bigsphere);
          this.world_.getCollPoints(bigsphere, set[s].collisionPoints);
        }


        if(set[0].collisionPoints.length > 0 || set[1].collisionPoints.length > 0 || set[2].collisionPoints.length > 0) {
          console.log('start');
          for(let j = 0; j !== 3; ++j) {
            if(set[j].collisionPoints.length > 0) print3('sphere', set[j].sphere, 'point', set[j].collisionPoints[0]);
          }
          console.log('end\n');
        }


        //print3('camPos', camPos);
        let displacement = this.doSpringRelaxation(set, false, true);
        if(!eq3(displacement, ZERO)) print3('disp', displacement);

        if(!eq3(displacement, ZERO) && displacement[2]/len3(displacement) > .5) this.onGround_ = true;

        displacement = this.doSpringRelaxation(set, this.onGround_ && eq3(this.moveImpulse_, ZERO), false);
        add3(camPos, displacement); // camPos += displacement
      }
    } // i !== 5 loop

    if(!this.onGround_) this.timeSinceOnGround_ += dt;
    else this.timeSinceOnGround_ = 0;

    camPosOut.set(camPos);
    camPosOut[2] -= this.camPosZDelta_;

    this.lastPos_.set(camPosOut);

    this.moveImpulse_.fill(0);
  }

  public doCamMovement(dt: number) {
    const keys_down = this.keyState_;

    let move_speed = 3.0;
    let turn_speed = 1.0;

    if(keys_down.has('ShiftLeft') || keys_down.has('ShiftRight')) { // south-paws
      move_speed *= 5;
      turn_speed *= 5;
    }

    let fwd: Float32Array, rgt: Float32Array;

    // TODO: Remove - temporary hack to zero velocity when controls released
    let pressed = false;

    if(keys_down.has('KeyW') || keys_down.has('ArrowUp')) {
      fwd = fromVector3(this.camForwardsVec_());
      addScaled3(this.moveImpulse_, fwd, move_speed);
      pressed = true;
    }
    if(keys_down.has('KeyS') || keys_down.has('ArrowDown')) {
      fwd = fwd ?? fromVector3(this.camForwardsVec_());
      addScaled3(this.moveImpulse_, fwd, -move_speed);
      pressed = true;
    }
    if(keys_down.has('KeyA') || keys_down.has('ArrowLeft')) { // TODO: Move the arrow keys later
      rgt = fromVector3(this.camRightVec_());
      addScaled3(this.moveImpulse_, rgt, -move_speed);
      pressed = true;
    }
    if(keys_down.has('KeyD') || keys_down.has('ArrowRight')) {
      rgt = rgt ?? fromVector3(this.camRightVec_());
      addScaled3(this.moveImpulse_, rgt, move_speed);
      pressed = true;
    }

    if(!pressed && !this.flyMode_) {
      // this.velocity_.fill(0); // HACK HACK HACK
    }

    // TODO: TURNING & JUMPING HERE
    const cP = this.camera_.position;
    const pos = new Float32Array([cP.x, cP.y, cP.z - EYE_HEIGHT]);
    this.update(dt, pos);

    // TODO: Remove, this code is temporary
    // if(pos[2] < 1.6) pos[2] = 1.6; // Stop falling through the ground

    this.camera_.position.set(pos[0], pos[1], pos[2] + EYE_HEIGHT);
    this.world_.ground.updateGroundPlane(pos);
  }

  // Returns displacement
  public doSpringRelaxation (sphereSet: SpringSphereSet[], constrainToVertical: boolean, fastMode: boolean): Float32Array {
    const displacement = new Float32Array(3);
    const force = new Float32Array(3);
    const currentspherepos = new Float32Array(3);
    const springvec = new Float32Array(3);
    const scaled = new Float32Array(3);

    const EPSILON_SQ = 1e-8;

    let iters = 0;
    const max_iters = fastMode ? 1 : 100;
    for(let i = 0; i !== max_iters; ++i) {
      iters += 1;

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
            mulScalar3(springvec, ss.sphere[3] - springLen);  // springvec = springvec * (radius - springlen)
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

  private tmp = [
    new Float32Array(3)
  ];

  // Packs 3 bounding spheres into the spheres array of [ s0x, s0y, s0z, s0radius, s1x, s1y, etc. ]
  public getCollisionSpheres(pos: Float32Array, spheres: Float32Array) {
    const [delta] = this.tmp;
    delta.fill(0);

    for(let i = 0; i !== 3; ++i) {
      delta[2] = i * (2 * SPHERE_RAD) - EYE_HEIGHT;
      add3(pos, delta, spheres.subarray(4*i, 4*i+3));
      spheres[4*i+3] = SPHERE_RAD;
    }

    // console.log('spheres', spheres);
  }
}