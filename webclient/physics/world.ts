/*
A representation of the world.  We might need to add a top-level acceleration structure to speed up sphere tracing and
general queries.

1) To start, we are going to do a linear scan of all the loaded meshes with the sphere tracing query.
2) Each object requires a BVH or at a minimum, a bounding AABB associated with the mesh.
3) TODO: Sort out building the BVH on a voxel mesh
4) Sphere vs AABB intersection distance and query
*/

import BVH, { Triangles } from './bvh.js';
import * as THREE from '../build/three.module.js';
import type { WorldObject } from '../webclient.js';
import Caster from './caster.js';
import {spherePathToAABB, sqDistSphereAABB, testAABB, transformAABB} from '../maths/geometry.js';
import { createAABBMesh } from './debug.js';
import { add3, len3, max3, min3, mulScalar3 } from '../maths/vec3.js';
import { EPSILON } from '../maths/defs.js';
import { print3 } from '../maths/functions.js';
import {clearSphereTraceResult, DIST, makeRay, makeSphereTraceResult, SphereTraceResult} from './types.js';
import { PlayerPhysics } from './player.js';
import { Ground } from './ground.js';

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

export default class PhysicsWorld {
  private readonly bvhIndex_: Map<string, BVH>;
  private readonly worldObjects_: Array<WorldObject>;
  private scene_: THREE.Scene | undefined;
  private caster_: Caster | undefined;
  private readonly player_: PlayerPhysics | undefined;

  private debugMeshes: DebugMesh[];
  private readonly tempMeshes: THREE.Group; // TODO: Remove before check-in

  // Ground Mesh
  private readonly ground_: Ground;

  public constructor (scene?: THREE.Scene, caster?: Caster) {
    this.scene_ = scene;
    this.bvhIndex_ = new Map<string, BVH>();
    this.worldObjects_ = new Array<WorldObject>();
    this.caster_ = caster;
    this.debugMeshes = [];
    this.tempMeshes = new THREE.Group();

    // TODO: Temporary - remove before checkin
    this.player_ = new PlayerPhysics(this);
    //this.player_.addAvatarBounds(this.tempMeshes);

    const sphere = new THREE.Mesh(new THREE.SphereGeometry(), new THREE.MeshStandardMaterial({color:'blue'}));
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
  public set caster (caster: Caster) {
    this.caster_ = caster;
    if(this.player_) this.player_.camera = caster.camera;
  }

  public get player (): PlayerPhysics { return this.player_; }
  public get ground (): Ground { return this.ground_; }
  public get worldObjects (): Array<WorldObject> { return this.worldObjects_; }

  // Check if BVH is loaded for model URL
  public hasModelBVH (key: string): boolean {
    return key in this.bvhIndex_;
  }

  public getModelBVH (key: string): BVH | undefined {
    return this.bvhIndex_[key];
  }

  public addModelBVH (key: string, triangles: Triangles): BVH {
    if(key in this.bvhIndex_) return this.bvhIndex_[key];
    const bvh = new BVH(triangles);
    this.bvhIndex_[key] = bvh;
    return bvh;
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

  public addWorldObject (obj: WorldObject, debug=false): number {
    let idx = this.worldObjects_.findIndex(e => e === null);
    if (idx === -1) {
      idx = this.worldObjects_.length;
      this.worldObjects_.push(obj);
    } else {
      this.worldObjects_[idx] = obj;
    }

    if(debug) {
      this.addDebugMesh(idx, DebugType.AABB_MESH, obj);
      this.addDebugMesh(idx, DebugType.TRI_MESH, obj);
      this.addDebugMesh(idx, DebugType.ROT_MESH, obj);
      //this.addDebugMesh(idx, DebugType.BVH_MESH, obj);
    }

    return idx;
  }

  public delWorldObject (objOrId: WorldObject | number): WorldObject | null {
    const idx = typeof objOrId === 'number' ? objOrId : this.worldObjects_.indexOf(objOrId);
    if (idx !== -1) {
      const obj = this.worldObjects_[idx];
      this.worldObjects_[idx] = null;
      return obj;
    }
    return null;
  }

  public traceRay (origin: THREE.Vector3, dir: THREE.Vector3): void {
    if(!this.caster_) return;

    for(let i = 0, end = this.worldObjects_.length; i !== end; ++i) {
      const obj = this.worldObjects_[i];
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

  // This function replicates the traceSphere function in the physics world in the Substrata C++ client.
  public traceSphereWorld (sphere: Float32Array, translation: Float32Array): SphereTraceResult {
    const spheres = new Float32Array(12);
    this.player_.getCollisionSpheres(sphere, spheres);

    const result = makeSphereTraceResult();
    const query = makeSphereTraceResult();

    const transLength = len3(translation);
    if(transLength < 1.0e-10) {
      // This check was done in PhysicsObject::traceSphere, moved it here as there is no need to repeat it
      console.log('zero length dir vector:', transLength);
      return result;
    }

    // Normalise the vector into a copy
    const dir = mulScalar3(translation, 1./transLength, new Float32Array(3));
    const spherePathAABB = spherePathToAABB(sphere, translation);

    let closest = Number.POSITIVE_INFINITY;

    for (let i = 0; i < this.worldObjects_.length; ++i) {
      const obj = this.worldObjects_[i];
      clearSphereTraceResult(query);
      const dist = this.traceSphereObject(obj, sphere, dir, transLength, spherePathAABB, query);
      if(dist !== -1 && closest > dist) {
        closest = dist;
        result.data.set(query.data);
        result.hit = obj;
        result.pointInTri = query.pointInTri;
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
    obj: WorldObject,
    sphere: Float32Array,
    dir: Float32Array,              // Normalised unit vector in direction of translation
    maxDist: number,                // Translation Length
    spherePathAABB: Float32Array,
    results: SphereTraceResult
  ): number {
    // If the world_aabb of this object does not intersect the spherePathAABB, return
    if(!testAABB(obj.world_aabb, spherePathAABB)) {
      results.data[DIST] = -1;
      return Number.POSITIVE_INFINITY;
    }

    const ray = makeRay();
    ray.origin.set(sphere.slice(0, 3));
    ray.dir.set(dir);
    ray.minmax[1] = maxDist;

    return obj.bvh.traceSphere(ray, obj.worldToObject, obj.objectToWorld, sphere[3], results);
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

    return this.ground.bvh.traceSphere(
      /*ray=*/ray,
      /*toObject=*/this.ground.worldToObject,
      /*toWorld=*/this.ground.objectToWorld,
      /*radius=*/sphere[3],
      /*results=*/results
    );
  }


  public getCollPointsObject (
    obj: WorldObject,
    sphere: Float32Array,
    sphereAABBWs: Float32Array,
    collisionPoints: Float32Array[]
  ): void {
    if(!testAABB(obj.world_aabb, sphereAABBWs)) return;

    obj.bvh.appendCollPoints(
      /*spherePosWs=*/sphere,
      /*radius=*/sphere[3],
      /*toObject=*/obj.worldToObject,
      /*toWorld=*/obj.objectToWorld,
      /*points=*/collisionPoints
    );
  }

  // An alternate interface for the constantly moving ground plane
  public getCollPointsGround (sphere: Float32Array, sphereAABBWs: Float32Array, collisionPoints: Float32Array[]) {
    this.ground.bvh.appendCollPoints(
      /*spherePosWs=*/sphere,
      /*radius=*/sphere[3],
      /*toObject=*/this.ground.worldToObject,
      /*toWorld=*/this.ground.objectToWorld,
      /*points=*/collisionPoints
    );
  }
}
