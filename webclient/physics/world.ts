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
import { sqDistSphereAABB, transformAABB } from '../maths/geometry.js';
import { createAABBMesh } from './debug.js';
import { add3, len3, max3, min3, mulScalar3 } from '../maths/vec3.js';
import { EPSILON } from '../maths/defs.js';
import { print3 } from '../maths/functions.js';
import { SphereTraceResult } from "./types";

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

  private debugMeshes: DebugMesh[];
  private readonly tempMeshes: THREE.Group; // TODO: Remove before check-in

  public constructor (scene?: THREE.Scene, caster?: Caster) {
    this.scene_ = scene;
    this.bvhIndex_ = new Map<string, BVH>();
    this.worldObjects_ = new Array<WorldObject>();
    this.caster_ = caster;
    this.debugMeshes = [];
    this.tempMeshes = new THREE.Group();

    const sphere = new THREE.Mesh(new THREE.SphereGeometry(), new THREE.MeshStandardMaterial({color:'blue'}));
    sphere.visible = false;
    this.tempMeshes.add(sphere);
  }

  // Getters/Setters
  public set scene (scene: THREE.Scene) {
    if(this.scene_ !== scene) scene.add(this.tempMeshes);
    this.scene_ = scene;
  }
  public get scene (): THREE.Scene { return this.scene_; }

  public set caster (caster: Caster) { this.caster_ = caster; }
  public get caster (): Caster { return this.caster_; }

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
        const transformed = transformAABB(obj.mesh.matrixWorld, aabb, new Float32Array(6));
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
      // this.addDebugMesh(idx, DebugType.BVH_MESH, obj);
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
      const [test, idx] = this.caster_.testRayBVH(origin, dir, obj.inv_world, obj.bvh);
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

  private tmp = [
    new Float32Array(4),
    new Float32Array(3),
  ];

  // Uses a ray casting method for speeding up intersection queries by computing the squared distance between an
  // advancing ray position to reduce the number of tests
  public traceSphere (sphere: Float32Array, dir: Float32Array, len=1000): [number, number] {
    const [P, d] = this.tmp;
    P[3] = sphere[3];

    const DEBUG = false;

    let t = 0;
    let idx = -1;
    let iterations = 0;

    while (t < len) {
      let ii = -1;
      let dist = Number.POSITIVE_INFINITY;
      mulScalar3(dir, t, d);
      add3(sphere, d, P);

      for(let i = 0; i < this.worldObjects_.length; ++i) {
        const obj = this.worldObjects_[i];
        const bound = obj.world_aabb;

        const sqD = sqDistSphereAABB(P, bound);
        if(sqD < dist) {
          dist = sqD;
          ii = i;
        }
      }

      if (dist <= EPSILON * t) {
        idx = ii;
        break;
      } else {
        t += Math.sqrt(dist);
        iterations += 1;
      }
    }

    if(idx !== -1) {
      if(DEBUG) {
        console.log(this.worldObjects_[idx].model_url, 'ray:', t, 'iter:', iterations);
        print3('point', P);
      }

      mulScalar3(dir, t, d);
      add3(sphere, d, P);

      // Set the debug sphere to the position of intersection
      this.tempMeshes.children[0].scale.set(sphere[3], sphere[3], sphere[3]);
      this.tempMeshes.children[0].position.set(P[0], P[1], P[2]);
      this.tempMeshes.children[0].visible = true;
    } else {
      this.tempMeshes.children[0].visible = false;
      if(DEBUG) console.log('iterations:', iterations);
    }

    return [Number.POSITIVE_INFINITY, -1];
  }

  // This function replicates the traceSphere function in the physics world in the Substrata C++ client.
  /*
  public traceSphereWorld (sphere: Float32Array, translation: Float32Array): SphereTraceResult {
    const result = {
      data: new Float32Array(7), // [ pos.x, pos.y, pos.z, nor.x, nor.y, nor.z, dist ]
      hit: undefined,
      pointInTri: undefined
    };

    // For now, just iterate over the objects
    const [P, d] = this.tmp;
    P[3] = sphere[3];
    const t = 0;

    // We don't use the concept of large objects yet...
    const MAX_LEN = len3(translation);
    const dir = mulScalar3(translation, MAX_LEN, new Float32Array(3));

    console.log('MAX_LENGTH:', MAX_LEN, 'dir:', dir, 'translation:', translation);

    const query = {
      data: new Float32Array(7),
      hit: undefined,
      pointInTri: undefined
    };

    const sphereAABB = new Float32Array([
      sphere[0], sphere[1], sphere[2],
      sphere[0], sphere[1], sphere[2]
    ]);

    const target = add3(sphere, translation, new Float32Array(3));

    min3(sphereAABB, 0, target);
    max3(sphereAABB, 3, target);

    for (let i = 0; i < this.worldObjects_.length; ++i) {
      const obj = this.worldObjects_[i];
      obj.bvh.traceSphere(sphere, translation, dir, MAX_LEN, query);
    }

    return result;
  }
  */
}