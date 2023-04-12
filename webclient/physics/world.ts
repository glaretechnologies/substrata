/*=====================================================================
world.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import BVH from './bvh.js';
import * as THREE from '../build/three.module.js';
import type { WorldObject } from '../worldobject.js';
import Caster from './caster.js';
import { makeAABB, transformAABB, spherePathToAABB, testAABB } from '../maths/geometry.js';
import { applyMatrix4, len3, mulScalar3, transformDirection } from '../maths/vec3.js';
import { clearSphereTraceResult, DIST, makeRay, makeSphereTraceResult, SphereTraceResult } from './types.js';
import { PlayerPhysics } from './player.js';
import { Ground } from './ground.js';
import { getBVHKey } from '../worldobject.js';

interface BVHRef {
	bvh: BVH,
	refCount: number;
}

export default class PhysicsWorld {
	private readonly bvhIndex_: Map<string, BVHRef>;
	private readonly worldObjects_: Array<WorldObject>;
	private readonly freeList_: Array<number>;
	private readonly player_: PlayerPhysics | undefined;
	private caster_: Caster | undefined;
	private scene_: THREE.Scene | undefined;

	private readonly tempMeshes: THREE.Group;

	// Ground Mesh
	private readonly ground_: Ground;

	public constructor (scene?: THREE.Scene, caster?: Caster) {
		this.scene_ = scene;
		this.bvhIndex_ = new Map<string, BVHRef>();
		this.worldObjects_ = new Array<WorldObject>();
		this.freeList_ = new Array<number>();
		this.caster_ = caster;
		this.tempMeshes = new THREE.Group();

		this.player_ = new PlayerPhysics(this);
		this.player_.addAvatarBounds(this.tempMeshes);

		const sphere = new THREE.Mesh(new THREE.SphereGeometry(), new THREE.MeshStandardMaterial({ color:'blue' }));
		sphere.visible = false;
		this.tempMeshes.add(sphere);

		this.ground_ = new Ground();
		this.tempMeshes.add(this.ground.mesh);
	}

	// Getters/Setters
	public get scene (): THREE.Scene { return this.scene_; }
	public set scene (scene: THREE.Scene) {
		if(this.scene_ !== scene) scene.add(this.tempMeshes);
		this.scene_ = scene;
	}

	public get caster (): Caster { return this.caster_; }
	public set caster (caster: Caster) { this.caster_ = caster; }

	public get player (): PlayerPhysics { return this.player_; }
	public get ground (): Ground { return this.ground_; }
	public get worldObjects (): Array<WorldObject> { return this.worldObjects_; }

	// Returns true if the bvh is already registered or if it is in the jobList to be loaded
	public hasModelBVH (key: string): boolean {
		return key in this.bvhIndex_; // || key in this.jobList;
	}

	public getModelBVH (key: string): BVH | undefined {
		return this.bvhIndex_[key]?.bvh;
	}

	// Updates the refCount for the bvhKey, returns true if 0
	private updateRefCount (key: string, delta: number): boolean {
		if(key in this.bvhIndex_) {
			this.bvhIndex_[key].refCount += delta;
			return this.bvhIndex_[key].refCount === 0;
		}
		// If not in index, return false as nothing needs to happen
		return false;
	}

	// This function registers the model and potentially initiates a request for the BVH - if the BVH is already in the
	// jobList, the additional request is queued and completed on receiving the built BVH
	//public registerWorldObject (bvhKey: string, obj: WorldObject, triangles?: Triangles, collidable=true): number {
	public registerWorldObject (bvhKey: string, obj: WorldObject, bvhIn?: BVH, collidable=true): number {
		const haveModel = this.hasModelBVH(bvhKey);
		if (!haveModel && bvhIn === null) {
			console.error('Cannot register BVH for:', bvhKey, this);
			//throw(new Error('bugger'));
			return -1;
		}

		let idx = this.freeList_.length === 0 ? -1 : this.freeList_.shift();
		if (idx === -1) {
			idx = this.worldObjects_.length;
			this.worldObjects_.push(obj);
		} else {
			this.worldObjects_[idx] = obj;
		}

		// Variables that are generally useful in other parts of the code stored on the object
		// We could move this inside the world class if necessary.
		obj.world_id = idx;

		const axis = new THREE.Vector3(obj.axis.x, obj.axis.y, obj.axis.z);
		axis.normalize();
		const rot_matrix = new THREE.Matrix4();
		rot_matrix.makeRotationAxis(axis, obj.angle);

		const scale_matrix = new THREE.Matrix4();
		scale_matrix.makeScale(obj.scale.x, obj.scale.y, obj.scale.z);

		const inv_scale_matrix = new THREE.Matrix4();
		inv_scale_matrix.makeScale(1.0 / obj.scale.x, 1.0 / obj.scale.y, 1.0 / obj.scale.z);

		const trans_matrix = new THREE.Matrix4();
		trans_matrix.makeTranslation(obj.pos.x, obj.pos.y, obj.pos.z);

		const inv_trans_matrix = new THREE.Matrix4();
		inv_trans_matrix.makeTranslation(-obj.pos.x, -obj.pos.y, -obj.pos.z);

		// T R S
		obj.objectToWorld = trans_matrix;
		obj.objectToWorld.multiply(rot_matrix);
		obj.objectToWorld.multiply(scale_matrix);

		rot_matrix.transpose(); //  rot_matrix is now R^-1

		// (T R S)^-1 = S^-1 R^-1 T^-1
		obj.worldToObject = inv_scale_matrix;
		obj.worldToObject.multiply(rot_matrix);
		obj.worldToObject.multiply(inv_trans_matrix);

		let aabb_os = makeAABB(obj.aabb_os_min, obj.aabb_os_max);
		obj.world_aabb = new Float32Array(6);
		transformAABB(obj.objectToWorld, aabb_os, obj.world_aabb);
		obj.collidable = collidable;

		const bvh = this.getModelBVH(bvhKey);
		if (bvh == null) {
			this.bvhIndex_[bvhKey] = {
				bvh: bvhIn,
				refCount: 1
			};
		} else {
			this.updateRefCount(bvhKey, 1);
			obj.bvh = bvh;
		}

		return idx;
	}

	// Note: Requires removal of object from jobList, use id for fast delete
	public delWorldObject (objOrId: WorldObject | number): WorldObject | null {
		const idx = typeof objOrId === 'number' ? objOrId : this.worldObjects_.indexOf(objOrId);
		if (idx !== -1) {
			const obj = this.worldObjects_[idx];
			this.worldObjects_[idx] = null;
			this.freeList_.push(idx);
			// If updateRefCount returns refCount === 0
			const bvhKey = getBVHKey(obj);
			if(this.updateRefCount(bvhKey, -1)) {
				delete this.bvhIndex_[bvhKey];
			}
			return obj;
		}

		return null;
	}

	// Find object of intersection
	public traceRay (origin: Float32Array, dir: Float32Array): number {
		let t = Number.POSITIVE_INFINITY;
		let hit = false;
		const O = new Float32Array(3), d = new Float32Array(3);
		for (let i = 0, end = this.worldObjects_.length; i !== end; ++i) {
			const obj = this.worldObjects_[i];
			if(obj && obj.bvh) {
				O.set(origin);
				d.set(dir);
				applyMatrix4(obj.worldToObject, O);
				transformDirection(obj.worldToObject, d);
				const test = obj.bvh.testRayLeaf(O, d);
				if (test[1] !== -1 && t > test[2]) {
					t = test[2];
					hit = true;
				}
			}
		}

		// Test ground
		O.set(origin); d.set(dir);
		applyMatrix4(this.ground.worldToObject, O);
		transformDirection(this.ground.worldToObject, d);
		const test = this.ground.bvh.testRayLeaf(O, d);
		if (test[1] !== -1 && t > test[2]) {
			t = test[2];
			hit = true;
		}

		return hit ? t : -1;
	}

	public pickWorldObject (origin: Float32Array, dir: Float32Array): WorldObject | null {
		let t = Number.POSITIVE_INFINITY;
		let hit: WorldObject | undefined;

		const O = new Float32Array(3), d = new Float32Array(3);
		for (let i = 0, end = this.worldObjects_.length; i !== end; ++i) {
			const obj = this.worldObjects_[i];
			if(obj && obj.bvh) {
				O.set(origin);
				d.set(dir);
				applyMatrix4(obj.worldToObject, O);
				transformDirection(obj.worldToObject, d);
				const test = obj.bvh.testRayLeaf(O, d);
				if (test[1] !== -1 && t > test[2]) {
					t = test[2];
					hit = obj;
				}
			}
		}
		return hit;
	}

	// This function replicates the traceSphere function in the physics world in the Substrata C++ client.
	public traceSphereWorld (sphere: Float32Array, translation: Float32Array): SphereTraceResult {
		const spheres = new Float32Array(12);
		this.player_.getCollisionSpheres(sphere, spheres);

		const result = makeSphereTraceResult();
		const query = makeSphereTraceResult();

		const transLength = len3(translation);
		if(transLength < 1.0e-10) {
			// This check was done in PhysicsObject::traceSphere, moved it here as there is no need to repeat it
			console.error('zero length dir vector:', transLength);
			return result;
		}

		// Normalise the vector into a copy
		const dir = mulScalar3(translation, 1./transLength, new Float32Array(3));
		const spherePathAABB = spherePathToAABB(sphere, translation);

		let closest = Number.POSITIVE_INFINITY;

		for (let i = 0; i < this.worldObjects_.length; ++i) {
			const obj = this.worldObjects_[i];
			if (obj && obj.bvh && obj.collidable) {
				clearSphereTraceResult(query);

				const dist = this.traceSphereObject(/*worldObj=*/obj, sphere, dir, /*maxDist=*/transLength, spherePathAABB,
					query);

				if(dist !== -1 && closest > dist) {
					closest = dist;
					result.data.set(query.data);
					result.hit = obj;
					result.pointInTri = query.pointInTri;
				}
			}
		}

		clearSphereTraceResult(query);
		const dist = this.traceSphereGround(sphere, dir, transLength, spherePathAABB, query);
		if(dist !== -1 && closest > dist) {
			result.data.set(query.data);
			result.hit = (this.ground_ as unknown) as WorldObject; // HACK - same relevant interface
			result.pointInTri = query.pointInTri;
		}

		return result;
	}

	// A port of PhysicsObject::traceSphere
	public traceSphereObject (
		worldObject: WorldObject,
		sphere: Float32Array,
		dir: Float32Array,              // Normalised unit vector in direction of translation
		maxDist: number,                // Translation Length
		spherePathAABB: Float32Array,
		results: SphereTraceResult
	): number {
		// If the world_aabb of this object does not intersect the spherePathAABB, return
		if(worldObject.bvh == null || !testAABB(worldObject.world_aabb, spherePathAABB)) {
			results.data[DIST] = -1;
			return Number.POSITIVE_INFINITY;
		}

		const ray = makeRay();
		ray.origin.set(sphere.slice(0, 3));
		ray.dir.set(dir);
		ray.minmax[1] = maxDist;

		return worldObject.bvh.traceSphere(/*ray=*/ray, /*toObject=*/worldObject.worldToObject,
			/*toWorld=*/worldObject.objectToWorld, /*radius=*/sphere[3], /*results=*/results);
	}

	public getCollPoints (sphere: Float32Array, collisionPoints: Float32Array[]): void {
		collisionPoints.splice(0, collisionPoints.length);
		const radius = sphere[3];
		const sphereAABB = new Float32Array([
			sphere[0] - radius, sphere[1] - radius, sphere[2] - radius,
			sphere[0] + radius, sphere[1] + radius, sphere[2] + radius
		]);

		// No Top-level Acceleration Structure yet
		for(let i = 0; i !== this.worldObjects_.length; ++i) {
			const obj = this.worldObjects[i];
			if(obj && obj.bvh && obj.collidable)
				this.getCollPointsObject(obj, sphere, sphereAABB, collisionPoints);
		}

		this.getCollPointsGround(sphere, sphereAABB, collisionPoints);
	}

	public traceSphereGround (
		sphere: Float32Array,
		dir: Float32Array,
		maxDist: number,
		spherePathAABB: Float32Array,
		results: SphereTraceResult
	) : number {
		// Ensure that the ground plane is positioned beneath the player before calling.
		const ray = makeRay();
		ray.origin.set(sphere.slice(0, 3));
		ray.dir.set(dir);
		ray.minmax[1] = maxDist;

		return this.ground.bvh.traceSphere(/*ray=*/ray, /*toObject=*/this.ground.worldToObject,
			/*toWorld=*/this.ground.objectToWorld, /*radius=*/sphere[3], /*results=*/results);
	}

	public getCollPointsObject (
		obj: WorldObject,
		sphere: Float32Array,
		sphereAABBWs: Float32Array,
		collisionPoints: Float32Array[]
	): void {
		if(!testAABB(obj.world_aabb, sphereAABBWs)) return;

		obj.bvh.appendCollPoints(/*spherePosWs=*/sphere, /*radius=*/sphere[3], /*toObject=*/obj.worldToObject,
			/*toWorld=*/obj.objectToWorld, /*points=*/collisionPoints);
	}

	// An alternate interface for the constantly moving ground plane
	public getCollPointsGround (sphere: Float32Array, sphereAABBWs: Float32Array, collisionPoints: Float32Array[]) {
		this.ground.bvh.appendCollPoints(/*spherePosWs=*/sphere, /*radius=*/sphere[3],
			/*toObject=*/this.ground.worldToObject, /*toWorld=*/this.ground.objectToWorld, /*points=*/collisionPoints);
	}

	/*
	These functions are old debugging functions that aren't really necessary any more
	private addDebugMesh (idx: number, type: DebugType, obj: WorldObject) {
		let mesh: THREE.Object3D;
		switch(type) {
		case DebugType.AABB_MESH: mesh = obj.bvh.getRootAABBMesh(); break;
		case DebugType.BVH_MESH: mesh = obj.bvh.getBVHMesh(); break;
		case DebugType.TRI_MESH: mesh = obj.bvh.getTriangleHighlighter(); break;
		case DebugType.ROT_MESH: {
			const aabb = obj.bvh.rootAABB;
			const transformed = transformAABB(obj.objectToWorld, aabb, new Float32Array(6));
			mesh = createAABBMesh(transformed);
			break;
		}
		}

		const debugMesh = {
			idx,
			type,
			mesh
		};

		if(type !== DebugType.ROT_MESH) {
			debugMesh.mesh.position.copy(obj.mesh.position);
			debugMesh.mesh.rotation.copy(obj.mesh.rotation);
			debugMesh.mesh.scale.copy(obj.mesh.scale);
		}
		debugMesh.mesh.frustumCulled = false;
		this.scene_.add(debugMesh.mesh);
		this.debugMeshes.push(debugMesh);
	}

	public debugRay (origin: Float32Array, dir: Float32Array): void {
		if(!this.caster_) return;

		for(let i = 0, end = this.worldObjects_.length; i !== end; ++i) {
			const obj = this.worldObjects_[i];
			if(obj && obj.bvh) { // Should collidable work on the picking rays?
				const [test, idx] = this.caster_.testRayBVH(origin, dir, obj.worldToObject, obj.bvh);
				if(test) {
					// See if we have any debug meshes associated to this mesh
					const set = this.debugMeshes.filter(e => e.idx === i);
					if(idx[0] !== -1) { // If we get a hit on an AABB (and potentially a triangle in idx[1])
						for(let i = 0; i !== set.length; ++i) {
							if(set[i].type === DebugType.AABB_MESH) {
								obj.bvh.updateAABBMesh(set[i].mesh, idx[0]);
							} else if(set[i].type === DebugType.TRI_MESH && idx[1] !== -1) {
								obj.bvh.updateTriangleHighlighter(idx[1], set[i].mesh);
							}
						}
					}
				}
			}
		}
	}
	*/
}
