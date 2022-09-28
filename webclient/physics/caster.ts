/*
Ray casting functions optimised to use the physics/collision detection geometry
*/

import * as THREE from '../build/three.module.js';
import { lerpN } from '../maths/functions.js';
import { DEG_TO_RAD } from '../maths/defs.js';
import BVH from './bvh.js';

// We can remove a lot of this configuration
export interface CasterConf {
  debugRay: boolean // on mouse press, draw ray in world space
  near: number // near plane
  far: number // far plane
  fov: number // the field of view
  camera?: THREE.PerspectiveCamera // the camera
  camRightFnc: () => THREE.Vector3
  camDirFnc: () => THREE.Vector3
}

const BOTTOM = 0;
const TOP = 1;

export default class Caster {
  private readonly rndr: THREE.WebGLRenderer;
  private conf: CasterConf;
  private readonly dims = new THREE.Vector2();
  private theta: number;
  private htan: number;
  private readonly frustum = new Float32Array(2);
  private readonly origin = new THREE.Vector3();
  private readonly U = new THREE.Vector3();
  private debugRayMesh: THREE.LineSegments | undefined;

  public constructor (renderer: THREE.WebGLRenderer, conf?: CasterConf) {
    this.rndr = renderer;
    // A lot of this config can be moved to a project-specific camera class
    this.config = conf ?? {
      debugRay: false,
      near: 0.1,
      far: 1000.0,
      fov: 75,
      camRightFnc: () => THREE.Vector3,
      camDirFnc: () => THREE.Vector3
    };
  }

  public set camera (cam: THREE.PerspectiveCamera) { this.conf.camera = cam; }
  public get camera (): THREE.PerspectiveCamera { return this.conf.camera; }

  public get config (): CasterConf { return this.conf; }
  public set config (conf: CasterConf) {
    if(conf.fov !== this.conf?.fov) {
      this.theta = 0.5 * conf.fov * DEG_TO_RAD;
      this.htan = Math.tan(this.theta);
      this.frustum[TOP] = conf.near * this.htan;
      this.frustum[BOTTOM] = -this.frustum[TOP];
    }
    this.conf = { ...this.conf, ...conf };
  }

  public createDebugRayMesh (): THREE.LineSegments {
    if(this.debugRayMesh) return this.debugRayMesh;

    const geo = new THREE.BufferGeometry();
    const buf = new Float32Array(6);
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false));
    this.debugRayMesh = new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'red'}));
    this.debugRayMesh.frustumCulled = false;
    return this.debugRayMesh;
  }

  // Calculate a pick ray based on the current camera view at screen coordinates [x, y]
  // pass setRay === true to draw the ray as a line segment in world space
  public getPickRay (x: number, y: number, setRay=false): [THREE.Vector3, THREE.Vector3] | null { // [ Origin, Dir ]
    this.rndr.getSize(this.dims);
    if(x < 0 || x > this.dims.x || y < 0 || y > this.dims.y) return null;

    const invH = 1. / this.dims.y;
    const AR = this.dims.x * invH;
    const r = x / this.dims.x, u = (this.dims.y - y) * invH;
    const right = AR * this.frustum[TOP]; const left = -right;

    const R = this.conf.camRightFnc();
    const D = this.conf.camDirFnc();
    this.U.crossVectors(D, R);
    R.multiplyScalar(lerpN(left, right, r));
    this.U.multiplyScalar(lerpN(this.frustum[TOP], this.frustum[BOTTOM], u));

    const dir = R.add(this.U).add(D.multiplyScalar(this.conf.near));
    dir.normalize();

    this.conf.camera.getWorldPosition(this.origin);

    if(this.conf.debugRay && setRay) {
      const buf = this.debugRayMesh.geometry.getAttribute('position');
      buf.set([
        this.origin.x, this.origin.y, this.origin.z,
        this.origin.x+10*dir.x, this.origin.y+10*dir.y, this.origin.z+10*dir.z
      ]);
      this.debugRayMesh.geometry.getAttribute('position').needsUpdate = true;
    }

    return [
      this.conf.camera.getWorldPosition(new THREE.Vector3()),
      dir
    ];
  }

  private tmp = [
    new THREE.Vector3(),
    new THREE.Vector3(),
    new Float32Array(3),
    new Float32Array(3)
  ];

  // Transform a ray from world space into the local space of the BVH and test for intersection
  public testRayBVH (origin: THREE.Vector3, dir: THREE.Vector3, invWorldMat: THREE.Matrix4, bvh: BVH): [boolean, number[]] {
    const [O, d, Of, df] = this.tmp;
    O.copy(origin); d.copy(dir);
    O.applyMatrix4(invWorldMat); d.transformDirection(invWorldMat);
    Of[0] = O.x; Of[1] = O.y; Of[2] = O.z;
    df[0] = d.x; df[1] = d.y; df[2] = d.z;

    return [bvh.testRayRoot(Of, df), bvh.testRayLeaf(Of, df)];
  }
}
