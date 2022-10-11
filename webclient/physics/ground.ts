/*
Adds a virtual ground plane to the physics world
*/

/*
1) Find player position.
2) Move ground plane to intersect player position
3) Don't need to store a ref to visual object, only need a BVH for intersection queries.  Use a
*/

import * as THREE from '../build/three.module.js';
import BVH, { Triangles } from './bvh.js';
import { generatePatch } from '../maths/generators.js';

export class Ground {
  private readonly bvh_: BVH;
  private readonly mesh_: THREE.Mesh;

  private readonly invWorld_: THREE.Matrix4;

  private readonly collisionPlane: THREE.Group;

  public constructor () {
    // This is a group at the origin into which we add our world space ground patches.
    this.collisionPlane = new THREE.Group();

    const geo = generatePatch(1, 1);
    const buf = new THREE.BufferGeometry();
    buf.setAttribute('position', new THREE.BufferAttribute(geo[0], 3, false));
    buf.setIndex(new THREE.BufferAttribute(geo[1], 1, false));
    this.mesh_ = new THREE.Mesh(buf, new THREE.MeshBasicMaterial({ color: 'red', transparent: true, opacity: 0.5 }));
    this.mesh_.position.set(0, 0, .001);
    this.mesh_.scale.set(10, 10, 10);

    const triangles = new Triangles(geo[0], geo[1], 0, 3); // Stride = 3 (multiples of 4 bytes) = 12 bytes per vertex
    this.bvh_ = new BVH(triangles);

    this.invWorld_ = new THREE.Matrix4();
  }

  // In order to visualise the ground collision plane
  public get mesh (): THREE.Mesh { return this.mesh_; }
  public get bvh (): BVH { return this.bvh_; }
  public get invWorld (): THREE.Matrix4 { return this.invWorld_; }
  public get matrixWorld (): THREE.Matrix4 { return this.mesh_.matrixWorld; }

  // Implemented method in MainWindow::UpdateGroundPlane
  public updateGroundPlane (camPos: Float32Array): void {
    //console.log('moving ground plane:', camPos);
    this.mesh.position.set(camPos[0], camPos[1], .001);
    this.invWorld.copy(this.mesh_.matrixWorld);
    this.invWorld.invert();
  }
}
