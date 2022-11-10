/*=====================================================================
world.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import BVH, { createIndex, getIndexType, IntIndex, Triangles } from './bvh.js';
import * as THREE from '../build/three.module.js';
import type { WorldObject } from '../worldobject.js';
import Caster from './caster.js';
import { makeAABB, spherePathToAABB, testAABB, transformAABB } from '../maths/geometry.js';
import { createAABBMesh } from './debug.js';
import { applyMatrix4, len3, mulScalar3, transformDirection } from '../maths/vec3.js';
import { clearSphereTraceResult, DIST, makeRay, makeSphereTraceResult, SphereTraceResult } from './types.js';
import { PlayerPhysics } from './player.js';
import { Ground } from './ground.js';
import { getBVHKey } from '../worldobject.js';

enum DebugType {
  AABB_MESH, // A single AABB, originally set to the BVH root AABB
  TRI_MESH, // A triangle for highlighting triangle picking
  BVH_MESH, // A BVH mesh visualising the leaf nodes of the BVH (visualising overlap)
  ROT_MESH// An AABB transformed from local space into world space
}

interface DebugMesh {
  idx: number;
  mesh: THREE.Object3D;
  type: DebugType
}

function extractWorkerResponse (result: WorkerResult, oldTriangles: Triangles): BVH {
	//const triangles = createTriangles(result);
	const vertices = new Float32Array(result.vertexBuf);
	const triIndex = createIndex(result.indexType as IntIndex, result.indexBuf);

	oldTriangles.transfer(vertices, triIndex);
	return new BVH(oldTriangles, {
		index: new Uint32Array(result.bvhIndexBuf),
		nodeCount: result.nodeCount,
		aabbBuffer: new Float32Array(result.aabbBuffer),
		dataBuffer: new Uint32Array(result.dataBuffer)
	});
}

interface BVHRef {
	bvh: BVH,
	refCount: number;
}

interface BVHJob {
	triangles: Triangles,
	worldObjectIds: Array<number>
}

export default class PhysicsWorld {
	private readonly bvhIndex_: Map<string, BVHRef>;
	private readonly worldObjects_: Array<WorldObject>;
	private readonly freeList_: Array<number>;
	private readonly player_: PlayerPhysics | undefined;
	private caster_: Caster | undefined;
	private scene_: THREE.Scene | undefined;

	private debugMeshes: DebugMesh[];
	private readonly tempMeshes: THREE.Group;

	// Ground Mesh
	private readonly ground_: Ground;

	// The BVH creation worker
	private readonly worker: Worker;
	private readonly jobList: Record<string, BVHJob>;

	public constructor (scene?: THREE.Scene, caster?: Caster) {
		this.scene_ = scene;
		this.bvhIndex_ = new Map<string, BVHRef>();
		this.worldObjects_ = new Array<WorldObject>();
		this.freeList_ = new Array<number>();
		this.caster_ = caster;
		this.debugMeshes = [];
		this.tempMeshes = new THREE.Group();

		this.player_ = new PlayerPhysics(this);
		this.player_.addAvatarBounds(this.tempMeshes);

		const sphere = new THREE.Mesh(new THREE.SphereGeometry(), new THREE.MeshStandardMaterial({ color:'blue' }));
		sphere.visible = false;
		this.tempMeshes.add(sphere);

		this.ground_ = new Ground();
		this.tempMeshes.add(this.ground.mesh);

		this.jobList = {};
		this.worker = new Worker('/webclient/physics/worker.js');
		this.worker.onmessage = ev => this.processWorkerResponse(ev);

		/*
		// Test individual textures on a quad at [0, 0, 4]
		const textureLoader = new TextureLoader(1);

		// Setup a quad that displays a compressed texture
		const mat = new THREE.MeshBasicMaterial({ map: null, side: THREE.DoubleSide });
		const quad = new THREE.Mesh(new THREE.PlaneGeometry(), mat);
		quad.position.set(0, 0, 4);
		quad.rotation.set(Math.PI/2, 0, 0);
		quad.scale.set(8, 8, 8);
		this.tempMeshes.add(quad);

		textureLoader.load('/webclient/testA.png',
			(tex: THREE.CompressedTexture) => {
				mat.map = tex;
				mat.needsUpdate = true;
			}
		);
	  */
	}

	// Process the Worker Response
	private processWorkerResponse(ev: MessageEvent<WorkerResult>): void {
		const job = this.jobList[ev.data.key];

		const bvh = extractWorkerResponse(ev.data as WorkerResult, job.triangles);

		let refCount = 0;
		for(let i = 0; i !== job.worldObjectIds.length; ++i) {
			const obj = this.worldObjects_[job.worldObjectIds[i]];
			// Only assign the bvh if the key matches (otherwise it has been unloaded and replaced with a different object)
			if(obj && getBVHKey(obj) === ev.data.key) {
				obj.bvh = bvh;
				refCount++;
			}
		}

		this.bvhIndex_[ev.data.key] = {
			bvh,
			refCount
		};

		delete this.jobList[ev.data.key];
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
		return key in this.bvhIndex_ || key in this.jobList;
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

	// Add one of several debug meshes
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

	// This function registers the model and potentially initiates a request for the BVH - if the BVH is already in the
	// jobList, the additional request is queued and completed on receiving the built BVH
	public registerWorldObject (bvhKey: string, obj: WorldObject, triangles?: Triangles, collidable=true): number {
		const haveModel = this.hasModelBVH(bvhKey);
		if (!haveModel && triangles === null) {
			console.error('Cannot construct BVH for:', bvhKey);
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
		obj.worldToObject = new THREE.Matrix4();
		obj.mesh.updateMatrixWorld(true);
		obj.worldToObject.copy(obj.mesh.matrixWorld);
		obj.worldToObject.invert();
		obj.world_aabb = makeAABB(obj.aabb_ws_min, obj.aabb_ws_max);
		obj.collidable = collidable;

		const bvh = this.getModelBVH(bvhKey);
		if (bvh == null) {
			if (!haveModel) { // The model isn't in the job queue, so build it - triangles must exist
				const indexType = getIndexType(triangles.index);
				const indexBuf = triangles.index.buffer;
				const vertexBuf = triangles.vertices.buffer;

				this.jobList[bvhKey] = {
					triangles,
					worldObjectIds: [idx]
				};

				this.worker.postMessage({
					id: idx,
					key: bvhKey,
					indexCount: triangles.index.length,
					vertexCount: triangles.vertices.length,
					stride: triangles.vert_stride,
					indexType: indexType,
					indexBuf,
					vertexBuf,
				}, [
					// Transfer memory contained in the Triangles structure to the worker
					indexBuf,
					vertexBuf,
				]);
			} else {
				this.jobList[bvhKey]?.worldObjectIds.push(idx);
			}
		} else {
			obj.bvh = bvh;
			this.updateRefCount(bvhKey, 1);
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
}

/*
// Testing Texture Compression & Workers
this.textureLoader_ = new TextureLoader(Math.max(1, Math.floor((navigator.hardwareConcurrency ?? 4) / 2)));

// Setup a quad that displays a compressed texture
const mat = new THREE.MeshBasicMaterial({ map: null, side: THREE.DoubleSide });
const quad = new THREE.Mesh(new THREE.PlaneGeometry(), mat);
quad.position.set(0, 0, 4);
quad.rotation.set(-Math.PI/2, 0, 0);
quad.scale.set(8, 8, 8);
this.tempMeshes.add(quad);

this.textureLoader_.load('/webclient/testB.jpg',
  (tex: THREE.CompressedTexture) => {
    mat.map = tex;
    mat.needsUpdate = true;
  }
);
*/

/*
this.textureLoader_.readPixels('/webclient/testB.jpg').then(task => {
  this.textureLoader_.compressTexture(task, (task: CompressionTask) => {
    const format = task.channels === 3 ? THREE.RGB_S3TC_DXT1_Format : THREE.RGBA_S3TC_DXT5_Format;
    const tex = new THREE.CompressedTexture(task.mipmaps, task.width, task.height, format);
    tex.wrapS = THREE.ClampToEdgeWrapping;
    tex.wrapT = THREE.ClampToEdgeWrapping;
    tex.flipY = true;
    tex.minFilter = THREE.LinearMipmapLinearFilter;
    tex.magFilter = THREE.LinearFilter;
    tex.needsUpdate = true;
    mat.map = tex;
    mat.needsUpdate = true;
  });
});
*/

/*
setTimeout(() => {
  this.textureLoader_.readPixels('/webclient/testB.jpg').then(task => {
    console.log('task:', task);

    this.textureLoader_.compressTexture(task, (task: CompressionTask) => {
      const format = task.channels === 3 ? THREE.RGB_S3TC_DXT1_Format : THREE.RGBA_S3TC_DXT5_Format;
      const tex = new THREE.CompressedTexture(task.mipmaps, task.width, task.height, format);
      tex.wrapS = THREE.ClampToEdgeWrapping;
      tex.wrapT = THREE.ClampToEdgeWrapping;
      tex.flipY = true;
      tex.minFilter = THREE.LinearMipmapLinearFilter;
      tex.magFilter = THREE.LinearFilter;
      tex.needsUpdate = true;
      mat.map = tex;
      mat.needsUpdate = true;
    });
  });
}, 5000);
*/