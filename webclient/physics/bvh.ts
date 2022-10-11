import * as THREE from '../build/three.module.js';
import { eq, print3, range } from '../maths/functions.js';
import { EPSILON } from '../maths/defs.js';
import {
  add3,
  applyMatrix4,
  cross3,
  dot3, len3,
  max3,
  min3,
  mulScalar3,
  normalise3, sqDist3, sqLen3,
  sub3,
  transformDirection
} from '../maths/vec3.js';
import {testAABB, testAABBV, testRayAABB, transformAABB, unionAABB} from '../maths/geometry.js';
import { makeRay, NOR_X, Ray, SphereTraceResult } from './types.js';
import { closestPointOnTriangle, pointInTri, Triangle } from '../maths/triangle.js';
import { closestPointOnPlane, Plane } from '../maths/plane.js';

export type IndexType = Uint32Array | Uint16Array | Uint8Array

/*
The triangles structure is built directly from the interleaved mesh buffer and associated index buffer.  The data is
therefore directly derived from the rendering data.
*/
export class Triangles {
  public vertices: Float32Array;
  public centroids: Float32Array;
  public index: IndexType;
  public readonly pos_offset: number;
  public readonly tri_count: number;
  public readonly vert_num: number;
  public readonly vert_stride: number;

  // Initialised with the existing interleaved buffer so that we don't need to copy any data
  public constructor (vertices: Float32Array, index: IndexType, offset: number, stride: number) {
    this.vertices = vertices;
    this.tri_count = Math.floor(index.length / 3);
    if(index.length % 3 !== 0) {
      console.warn('index length not a multiple of 3', this.tri_count);
    }

    this.vert_num = Math.floor(vertices.length / stride);
    if(vertices.length % stride !== 0) {
      console.warn('vertices length not a multiple of stride', this.vert_num);
    }

    this.index = index;
    this.pos_offset = offset; // The offset of the position attribute in the interleaved buffer
    this.vert_stride = stride; // The stride for the vertex
  }

  // Get the Vertex id (vertex) for the triangle id (tri) in the 3-component output
  public getTriVertex (tri: number, vertex: number, output: Float32Array): Float32Array {
    const voff = this.vert_stride * this.index[3 * tri + vertex] + this.pos_offset;
    output[0] = this.vertices[voff]; output[1] = this.vertices[voff+1]; output[2] = this.vertices[voff+2];
    return output;
  }

  // Get the centroid for triangle id (tri) in the 3-component output
  public getCentroid (tri: number, output: Float32Array): Float32Array {
    const coff = 3 * tri;
    output[0] = this.centroids[coff]; output[1] = this.centroids[coff+1]; output[2] = this.centroids[coff+2];
    return output;
  }

  // Temporaries that we create once and avoid recreating each time we call the intersectTriangle function.
  // These instances take time to create so we prefer destructuring them into the function (which is very fast).
  private tmp: Float32Array[] = [
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3)
  ];

  // Moeller-Trumbore ray triangle intersection
  public intersectTriangle (origin: Float32Array, dir: Float32Array, triIndex: number): number | null {
    // Destructure the array into the named variables we'll use in the function
    const [v0, v1, v2, e0, e1, h, s, q] = this.tmp;

    this.getTriVertex(triIndex, 0, v0);
    this.getTriVertex(triIndex, 1, v1);
    this.getTriVertex(triIndex, 2, v2);

    sub3(v1, v0, e0);           // e0 = v1 - v0
    sub3(v2, v0, e1);           // e1 = v2 - v0
    cross3(dir, e1, h);         // h = dir x e1

    const a = dot3(e0, h);
    if (eq(a, 0, EPSILON)) return null;

    const f = 1.0 / a;
    sub3(origin, v0, s);        // s = origin - v0
    const u = f * dot3(s, h);
    if (u < 0 || u > 1) return null;

    cross3(s, e0, q);           // q = s x e0
    const v = f * dot3(dir, q);
    if (v < 0 || u + v > 1) return null;

    const t = f * dot3(e1, q);
    return t > EPSILON ? t : null;
  }

  // Traverse the collection of triangles and build the centroids
  public buildCentroids (): void {
    if(!this.centroids) this.centroids = new Float32Array(this.tri_count * 3);

    for(let tri = 0, idx = 0; tri < this.tri_count; ++tri, idx += 3) {
      const A = this.vert_stride * this.index[idx] + this.pos_offset;
      const B = this.vert_stride * this.index[idx+1] + this.pos_offset;
      const C = this.vert_stride * this.index[idx+2] + this.pos_offset;

      for(let i = 0; i < 3; ++i) {
        this.centroids[idx+i] = (this.vertices[A+i] + this.vertices[B+i] + this.vertices[C+i])/3;
      }
    }
  }
}

export type heuristicFunction = (nodeIdx: number) => [number, number]

/*
The BVH nodes are directly stored in two arrays
*/
export default class BVH {
  // Stores the AABB for each node in six consecutive floats
  private readonly aabbBuffer: Float32Array; // 6 x floats for each BVN node [minx, miny, minz, maxx, maxy, maxz]
  // Stores data per node in 2 consecutive uint32s - offset and count
  // If count is 0 then offset stores the left child of the branch, the rightChild = (leftChild + 1)
  // If count is > 0 then offset refers to the consecutive triangle indices stored in the index array.
  private readonly dataBuffer: Uint32Array; // 2 x uint32s per node [offset, count]
  // Total memory usage per node = (6 + 2) x 4 = 32 bytes

  private readonly tri: Triangles;
  private readonly index: Uint32Array; // The triangle index stored in consecutive ranges in the nodes
  private readonly maxNodes: number;
  private nodeCount: number; // Number of used nodes

  private readonly maxNodeSize = 2; // The max node size before splitting (we allow quads)

  private heuristic_fnc: heuristicFunction;

  // Temporaries used in functions to avoid creating new arrays on each call
  private readonly tmp = [
    new Float32Array(3),
    new Float32Array(3),
    new Float32Array(3)
  ];

  public constructor (tri: Triangles) {
    this.tri = tri;
    this.index = range(tri.tri_count);
    this.maxNodes = 2 * tri.tri_count - 1; // TODO: Trim after building
    this.aabbBuffer = new Float32Array(this.maxNodes * 6);
    this.dataBuffer = new Uint32Array(this.maxNodes * 2);

    if(!tri.centroids) tri.buildCentroids();

    // Set the default heuristic for split cost calculation
    this.heuristic_fnc = this.computeMean;

    // Set the root node with the all the triangles...
    // Set offset = 0 and count = tri.tri_count for node 0
    this.dataBuffer[0] = 0; this.dataBuffer[1] = tri.tri_count;
    this.nodeCount = 1;

    // Compute bound of node 0 and recursively split node 0
    this.computeAABB(0);
    this.split(0);
  }

  // Boolean Result
  public testRayRoot (origin: Float32Array, dir: Float32Array): boolean {
    //print3('origin', origin, 'dir', dir)
    const query = testRayAABB(origin, dir, this.aabbBuffer);
    return query[0];
  }

  // Returns the index of the BVH node, and triangle of intersection with the ray
  public testRayLeaf (origin: Float32Array, dir: Float32Array): [number, number] { // [nodeIdx, triIdx]
    let t_min = Number.MAX_VALUE;
    let idx = -1;
    let tri_idx = -1;
    const q = [0];
    while (q.length > 0) {
      const curr = q.shift();
      const isLeaf = this.dataBuffer[2*curr+1] > 0;
      const [test] = testRayAABB(origin, dir, this.aabbBuffer, 6 * curr);
      if (test) {
        if (isLeaf) {
          const offset = this.dataBuffer[2 * curr], count = this.dataBuffer[2 * curr + 1];
          for (let i = 0; i < count; ++i) {
            const tri = this.index[offset + i];
            const t = this.tri.intersectTriangle(origin, dir, tri);
            if (t != null && t < t_min) {
              t_min = t;
              tri_idx = tri;
              idx = curr;
            }
          }
        } else {
          q.push(this.dataBuffer[2*curr], this.dataBuffer[2*curr]+1);
        }
      }
    }

    return [idx, tri_idx];
  }

  public get rootAABB (): Float32Array {
    return this.aabbBuffer.slice(0, 6);
  }

  // Compute the AABB for the input node and store the result in the AABB buffer
  private computeAABB (nodeIdx: number) {
    const aabb = this.aabbBuffer, data = this.dataBuffer;
    const min_idx = 6 * nodeIdx, max_idx = min_idx + 3, data_idx = 2 * nodeIdx;

    aabb.fill(Number.MAX_VALUE, min_idx, max_idx);
    aabb.fill(-Number.MAX_VALUE, max_idx, max_idx+3);

    const offset = data[data_idx];
    const count = data[data_idx+1];
    const [t0] = this.tmp;

    for(let i = 0; i < count; ++i) {
      const tri = this.index[offset+i];
      // Read triangle tri, vertex 0 into t0
      this.tri.getTriVertex(tri, 0, t0);
      min3(aabb, min_idx, t0, 0);
      max3(aabb, max_idx, t0, 0);
      // Read triangle tri, vertex 1 into t0
      this.tri.getTriVertex(tri, 1, t0);
      min3(aabb, min_idx, t0, 0);
      max3(aabb, max_idx, t0, 0);
      // Read triangle tri, vertex 2 into t0
      this.tri.getTriVertex(tri, 2, t0);
      min3(aabb, min_idx, t0, 0);
      max3(aabb, max_idx, t0, 0);
    }
  }

  // Keep the tree somewhat balanced (mean is a better heuristic than median)
  private computeMean (nodeIdx: number): [number, number] | null {
    const data_idx = 2 * nodeIdx;
    const offset = this.dataBuffer[data_idx];
    const count = this.dataBuffer[data_idx+1];

    if(count === 0) return null;

    const [t0, t1] = this.tmp;
    t0.fill(0);

    for(let i = 0; i !== count; ++i) {
      // Read the triangle centroid into t1
      this.tri.getCentroid(this.index[offset + i], t1);
      // Accumulate the centroids
      t0[0] += t1[0]; t0[1] += t1[1]; t0[2] += t1[2];
    }

    // Average
    const inv = 1./count;
    t0[0] *= inv; t0[1] *= inv; t0[2] *= inv;

    const buf = this.aabbBuffer;
    const min = 6*nodeIdx, max = min+3;
    // Compute the max extents
    t1[0] = (buf[max] - buf[min]); t1[1] = (buf[max+1] - buf[min+1]); t1[2] = buf[max+2] - buf[min+2];
    const longest = Math.max(...t1);
    // Choose the longest axis for the split
    for(let i = 0; i !== 3; ++i) if(longest === t1[i]) return [t0[i], i];

    return null;
  }

  // Try the SAH.  Probably too slow, and we should prefer building speed over query speed as we will do few queries
  // per frame
  private computeSAH (idx: number): [number, number] | null {
    return null;
  }

  // Compute split of parent AABB on longest axis
  private computeLongestSplit (idx: number): [number, number] | null {
    const [t0] = this.tmp;
    const buf = this.aabbBuffer;
    const min = 6*idx, max = min+3;
    // Compute extents of aabb
    t0[0] = (buf[max] - buf[min]); t0[1] = (buf[max+1] - buf[min+1]); t0[2] = buf[max+2] - buf[min+2];
    const longest = Math.max(...t0);
    // Split on longest axis and compute centre as split point
    for(let i = 0; i !== 3; ++i) if(longest === t0[i]) return [buf[min+i] + t0[i]/2., i];
    return null;
  }

  // Organise a range of triangles into left and right bins at splitPoint along axis
  private partition (offset: number, count: number, splitPoint: number, axis: number): number {
    const [centroid] = this.tmp;

    let left = offset, right = offset + count - 1;
    while (left <= right) {
      const tri = this.index[left];
      this.tri.getCentroid(tri, centroid);
      if(centroid[axis] < splitPoint) left += 1;
      else { // Swap to the front of the right
        this.index[left] = this.index[right];
        this.index[right] = tri;
        right -= 1;
      }
    }
    return left;
  }

  public set heuristic (fnc: (nodeIdx: number) => [number, number]) {
    this.heuristic_fnc = fnc;
  }

  // Split an input node (if possible) based on selected heuristic
  private split (nodeIdx: number) {
    const data = this.dataBuffer;
    const curr_off = 2 * nodeIdx;
    const offset = data[curr_off], count = data[curr_off+1];
    if(count <= this.maxNodeSize) return; // Don't split further, node is already small enough

    const [splitPoint, axis] = this.heuristic_fnc(nodeIdx);
    const cut = this.partition(offset, count, splitPoint, axis);

    // We have left in data[offset, cut] and right in data[cut, offset+count]
    const leftCount = cut - offset, rightCount = count - leftCount;
    if(leftCount === 0 || rightCount === 0) return;

    const leftNode = this.nodeCount++;
    const rightNode = this.nodeCount++;

    // Store the left and right triangle collections in the two consecutive nodes
    data[2*leftNode] = offset; data[2*leftNode+1] = leftCount;
    data[2*rightNode] = cut; data[2*rightNode+1] = rightCount;
    // Set the split node to the index of the left child & count to 0
    data[curr_off] = leftNode; data[curr_off+1] = 0;

    // Compute the bound for each node and try to split each
    this.computeAABB(leftNode); this.computeAABB(rightNode);
    this.split(leftNode); this.split(rightNode);
  }

  // Build a visualisation of a leaves-only BVH tree
  public getBVHMesh (): THREE.LineSegments {
    const icount = this.nodeCount * 24; // Max possible nodes, TODO optimise this
    const buf = new Float32Array(icount);
    const idx = new Uint32Array(icount);

    let used = 0;
    // Use a queue rather than recursion to traverse the tree.
    const queue = [0];
    while(queue.length > 0 && used < this.nodeCount) {
      const node = queue.shift();
      const offset = this.dataBuffer[2 * node], count = this.dataBuffer[2 * node + 1];
      if(count === 0) { // If count is zero, this is a branch so push to two consecutive child nodes
        queue.push(offset, offset+1);
      } else {
        // Build an AABB box into the mesh
        this.buildAABB(node, buf, idx, 24 * used);
        used += 1;
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));

    // We only use the used number of leaf nodes thus wasting some memory
    // TODO: to be optimised
    geo.setDrawRange(0, used*24);

    return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'green'}));
  }

  private buildAABB (idx: number, vertices: Float32Array, index: Uint32Array, offset: number) {
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(6*idx, 6*(idx+1));
    let i = offset;

    vertices[i++] = minx; vertices[i++] = miny; vertices[i++] = maxz; // 0
    vertices[i++] = minx; vertices[i++] = miny; vertices[i++] = minz; // 1
    vertices[i++] = minx; vertices[i++] = maxy; vertices[i++] = maxz; // 2
    vertices[i++] = minx; vertices[i++] = maxy; vertices[i++] = minz; // 3
    vertices[i++] = maxx; vertices[i++] = miny; vertices[i++] = maxz; // 4
    vertices[i++] = maxx; vertices[i++] = miny; vertices[i++] = minz; // 5
    vertices[i++] = maxx; vertices[i++] = maxy; vertices[i++] = maxz; // 6
    vertices[i++] = maxx; vertices[i++] = maxy; vertices[i++] = minz; // 7

    i = offset;
    const off_idx = offset / 3;
    index[i++] = off_idx;   index[i++] = off_idx+1; index[i++] = off_idx+1; index[i++] = off_idx+3;
    index[i++] = off_idx;   index[i++] = off_idx+2; index[i++] = off_idx+2; index[i++] = off_idx+3;
    index[i++] = off_idx+4; index[i++] = off_idx+5; index[i++] = off_idx+5; index[i++] = off_idx+7;
    index[i++] = off_idx+4; index[i++] = off_idx+6; index[i++] = off_idx+6; index[i++] = off_idx+7;
    index[i++] = off_idx;   index[i++] = off_idx+4; index[i++] = off_idx+2; index[i++] = off_idx+6;
    index[i++] = off_idx+1; index[i++] = off_idx+5; index[i++] = off_idx+3; index[i++] = off_idx+7;
  }

  // Build an AABB mesh on the root of the BVH
  public getRootAABBMesh (): THREE.LineSegments {
    const buf = new Float32Array(24);
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(0, 6);

    let i = 0;
    buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz; // 0
    buf[i++] = minx; buf[i++] = miny; buf[i++] = minz; // 1
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz; // 2
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz; // 3
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz; // 4
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz; // 5
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz; // 6
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz; // 7

    const idx = new Uint8Array(24);

    i = 0;
    // left
    idx[i++] = 0; idx[i++] = 1; idx[i++] = 1; idx[i++] = 3;
    idx[i++] = 0; idx[i++] = 2; idx[i++] = 2; idx[i++] = 3;
    // right
    idx[i++] = 4; idx[i++] = 5; idx[i++] = 5; idx[i++] = 7;
    idx[i++] = 4; idx[i++] = 6; idx[i++] = 6; idx[i++] = 7;
    // front
    idx[i++] = 0; idx[i++] = 4; idx[i++] = 2; idx[i++] = 6;
    // back
    idx[i++] = 1; idx[i++] = 5; idx[i++] = 3; idx[i++] = 7;

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));

    return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'red'}));
  }

  // Update a single AABB mesh with updated data
  public updateAABBMesh (mesh: THREE.LineMesh, idx: number): void {
    const attr = mesh.geometry.getAttribute('position');
    if(!attr) return;

    const buf = attr.array;
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(6*idx, 6*(idx+1));
    let i = 0;
    buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz;
    buf[i++] = minx; buf[i++] = miny; buf[i++] = minz;
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz;
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz;
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz;
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz;
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz;
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz;

    attr.needsUpdate = true;
  }

  // Create a triangle highlighter to highlight the outline of triangles on the mesh represented by this BVH
  public getTriangleHighlighter (): THREE.LineLoop {
    const buffer = new Float32Array(9);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(buffer, 3, false));
    const mat = new THREE.LineBasicMaterial({color: 'blue', depthTest: false});
    return new THREE.LineLoop(geo, mat);
  }

  // Update the triangle highlighter to the triangle represented by triangleIdx
  public updateTriangleHighlighter (triangleIdx: number, mesh: THREE.LineLoop): void {
    const attr = mesh.geometry.getAttribute('position');
    if(!attr) return;

    const buffer = attr.array;
    const [v0, v1, v2] = this.tmp;
    this.tri.getTriVertex(triangleIdx, 0, v0);
    this.tri.getTriVertex(triangleIdx, 1, v1);
    this.tri.getTriVertex(triangleIdx, 2, v2);
    buffer.set(v0, 0); buffer.set(v1, 3); buffer.set(v2, 6);
    attr.needsUpdate = true;
  }

  // Debug code for testing assignment and other properties
  traverse () {
    const back = this.nodeCount;
    const data = this.dataBuffer;
    const aabb = this.aabbBuffer;

    // If we walk from the back, do we get all triangles?
    for (let i = back - 1; i >= 0; --i) {
      const idx = 2*i;
      console.log('node:', i, 'offset', data[idx], 'count:', data[idx+1], 'isLeaf:', data[idx+1] !== 0);
    }

    // Are all triangles reachable...?
    let ids = [];
    const q = [0];
    while (q.length > 0) {
      const curr = q.shift();
      const offset = data[2*curr], count = data[2*curr+1];
      if(count === 0) {
        q.push(offset, offset+1);
      } else {
        const tris = Array.from(range(count, offset));
        console.log('tris:', tris);
        print3('min', aabb.slice(6*curr, 6*curr + 3), 'max', aabb.slice(6*curr+3, 6*curr+6));
        ids = [...ids, ...tris];
      }
    }

    const output = new Set(ids);
    console.log('output', output);
  }

  // See glare-core BVH::traceSphere
  public traceSphere (
    ray: Ray,
    toObject: THREE.Matrix4,
    toWorld: THREE.Matrix4,
    radius: number,
    result: SphereTraceResult
  ): number {
    const localRay = makeRay(ray);
    //localRay.origin.set(ray.origin);
    //localRay.dir.set(ray.dir);
    //localRay.minmax.set(ray.minmax);

    const start_ws = new Float32Array([
      ray.origin[0] - radius, ray.origin[1] - radius, ray.origin[2] - radius,
      ray.origin[0] + radius, ray.origin[1] + radius, ray.origin[2] + radius
    ]);

    const tx = ray.origin[0] + ray.dir[0] * ray.minmax[1];
    const ty = ray.origin[1] + ray.dir[1] * ray.minmax[1];
    const tz = ray.origin[2] + ray.dir[2] * ray.minmax[1];

    const end_ws = new Float32Array([
      tx - radius, ty - radius, tz - radius,
      tx + radius, ty + radius, tz + radius
    ]);

    const start_os = transformAABB(toObject, start_ws, new Float32Array(6));
    const end_os = transformAABB(toObject, end_ws, new Float32Array(6));

    const sphere_aabb_os = unionAABB(start_os, end_os);

    const stack = new Uint32Array(64);
    let top = 0;
    stack[top] = 0;

    const data = this.dataBuffer;
    const aabb = this.aabbBuffer;
    let breakInner = false;


    while(top >= 0) {
      breakInner = false;
      let curr = stack[top--];
      let count = data[2*curr+1];

      while (count === 0 && !breakInner) { // While on an interior node
        const offset = data[2*curr];
        const left = 6 * offset, right = 6 * (offset+1);

        // Inverted tests
        const left_hit = testAABBV(aabb, left, sphere_aabb_os, 0);
        const right_hit = testAABBV(aabb, right, sphere_aabb_os, 0);

        // console.log('o:', offset, 'c:', data[2*curr+1], left_hit, right_hit);

        if(left_hit) {
          if(right_hit) {
            top++;
            stack[top] = offset; // push the left node onto the stack
            curr = offset+1; // set curr to the right node
            count = data[2*curr+1]; // update the count
          } else {
            curr = offset; // set current to the left
            count = data[2*curr+1];
          }
        } else {
          if(right_hit) {
            curr = offset+1;
            count = data[2*curr+1];
          } else {
            breakInner = true;
          }
        }
      }

      if(!breakInner) { // This is a leaf with triangles associated
        const offset = data[2*curr], count = data[2*curr+1];
        for(let i = 0; i !== count; ++i) {
          const tri = this.index[offset+i];
          //console.log('testing tri:', tri);
          this.intersectSphereTri(localRay, toWorld, radius, tri, result);
        }
      }
    }

    return localRay.minmax[1] < ray.minmax[1] ? localRay.minmax[1] : -1.0;
  }

  private intersectSphereTri(
    ray: Ray,
    toWorld: THREE.Matrix4,
    radius: number,
    triIndex: number,
    result: SphereTraceResult
  ): void {
    // Optimise later...
    const sourcePoint_ws = new Float32Array(ray.origin);
    const unitdir_ws = new Float32Array(ray.dir);

    // TODO: Optimise this
    const v0 = new Float32Array(3), v1 = new Float32Array(3), v2 = new Float32Array(3);
    const e0 = new Float32Array(3), e1 = new Float32Array(3);

    this.tri.getTriVertex(triIndex, 0, v0); // Read triangle vertices into vectors
    this.tri.getTriVertex(triIndex, 1, v1);
    this.tri.getTriVertex(triIndex, 2, v2);
    sub3(v1, v0, e0);                 // e0 = v1 - v0
    sub3(v2, v0, e1);                 // e1 = v2 - v0

    applyMatrix4(toWorld, v0);        // Stores result in v0 so v0 now in world space
    transformDirection(toWorld, e0);  // Reason for doing this in world space and not object space?
    transformDirection(toWorld, e1);

    const normal = new Float32Array(3);
    cross3(e0, e1, normal);           // normal = e0 x e1
    normalise3(normal);

    const tri: Triangle = { v0, e0, e1, normal };
    const plane: Plane = { normal, D: dot3(v0, normal) };

    // Signed distance
    let pDist = dot3(sourcePoint_ws, plane.normal) - plane.D;
    const useNormal = new Float32Array(normal);
    if(pDist < 0) {
      //console.log('flipping normal:', useNormal);
      mulScalar3(useNormal, -1); // useNormal *= -1
      pDist *= -1;
    }

    const approach_rate = -dot3(useNormal, unitdir_ws);
    if(approach_rate <= 0) {
      //console.log('leaving');
      return;
    }

    const trans_len_needed = (pDist - radius) / approach_rate;
    //console.log('trans_len_needed:', trans_len_needed);

    if(ray.minmax[1] < trans_len_needed) {
      //console.log('too far', ray.minmax[1]);
      return;
    }

    const planeIntersectionP = new Float32Array(3);
    if(trans_len_needed <= 0) {
      closestPointOnPlane(plane, sourcePoint_ws, planeIntersectionP);
    } else {
      const tmp0 = new Float32Array(3);
      const tmp1 = new Float32Array(3);

      mulScalar3(useNormal, radius, tmp0);            // tmp0 = useNormal * radius
      mulScalar3(unitdir_ws, trans_len_needed, tmp1); // tmp1 = unitdir_ws * trans_len_needed
      sub3(tmp1, tmp0);                               // tmp1 -= tmp0
      add3(sourcePoint_ws, tmp1, planeIntersectionP); // planeIntersectionP = sourcePoint + (tmp1 - tmp0)
      //print3('planeIntersectionP', planeIntersectionP);
    }

    const triIntersectionPoint = new Float32Array(planeIntersectionP);
    const point_in_tri = pointInTri(tri, triIntersectionPoint);
    let dist;
    //console.log('point_in_tri:', point_in_tri);
    if(point_in_tri) {
      dist = Math.max(0, trans_len_needed);
    } else {
      const tmp = new Float32Array(3);
      closestPointOnTriangle(tri, triIntersectionPoint, tmp);
      triIntersectionPoint.set(tmp);
      dist = traceRayAgainstSphere(triIntersectionPoint, ray.dir, ray.origin, radius);
    }

    //console.log('dist:', dist);

    if(dist >= 0 && dist < ray.minmax[1]) {
      ray.minmax[1] = dist;
      result.data.set(triIntersectionPoint);
      result.pointInTri = point_in_tri;
      result.data[6] = dist;
      //console.log('setting output');

      if(point_in_tri) result.data.set(useNormal, NOR_X);
      else {
        const tmp = new Float32Array(3);
        mulScalar3(unitdir_ws, dist, tmp); // tmp = unitdir_ws * dist
        add3(tmp, sourcePoint_ws); // tmp += sourcePoint_ws
        sub3(tmp, triIntersectionPoint); // tmp -= triIntersectionPoint
        normalise3(tmp);
        result.data.set(tmp, NOR_X);
        //console.log('tri result:', result);
      }
    }
  }

  // BVH::DistType BVH::traceSphere(const Ray& ray_ws_, const Matrix4f& to_object, const Matrix4f& to_world,
  // float radius_ws, Vec4f& hit_pos_ws_out, Vec4f& hit_normal_ws_out, bool& point_in_tri_out) const

  // void BVH::appendCollPoints(const Vec4f& sphere_pos_ws, float radius_ws, const Matrix4f& to_object,
  // const Matrix4f& to_world, std::vector<Vec4f>& points_ws_in_out) const

  public appendCollPoints (
    spherePosWs: Float32Array,
    radius: number,
    toObject: THREE.Matrix4,
    toWorld: THREE.Matrix4,
    points: Array<Float32Array> // TODO: make resizable buffer of Float32Array
  ) : void {
    const sqRad = radius*radius;
    const sphere_aabb_ws = new Float32Array([
      spherePosWs[0] - radius, spherePosWs[1] - radius, spherePosWs[2] - radius,
      spherePosWs[0] + radius, spherePosWs[1] + radius, spherePosWs[2] + radius,
    ]);
    const sphere_aabb_os = transformAABB(toObject, sphere_aabb_ws, new Float32Array(6));

    const stack = new Uint32Array(64);
    let top = 0;
    stack[top] = 0;

    const data = this.dataBuffer;
    const aabb = this.aabbBuffer;
    let breakInner = false;

    while(top >= 0) {
      breakInner = false;
      let curr = stack[top--];
      let count = data[2*curr+1];

      while (count === 0 && !breakInner) { // While on an interior node
        const offset = data[2*curr];
        const left = 6 * offset, right = 6 * (offset+1);

        // Inverted tests
        const left_hit = testAABBV(aabb, left, sphere_aabb_os, 0);
        const right_hit = testAABBV(aabb, right, sphere_aabb_os, 0);

        if(left_hit) {
          if(right_hit) {
            top++;
            stack[top] = offset; // push the left node onto the stack
            curr = offset+1; // set curr to the right node
            count = data[2*curr+1]; // update the count
          } else {
            curr = offset; // set current to the left
            count = data[2*curr+1]; // update The count
          }
        } else {
          if(right_hit) {
            curr = offset+1; // set curr to the right node
            count = data[2*curr+1]; // update the count
          } else {
            breakInner = true;
          }
        }
      }

      if(!breakInner) {
        const offset = data[2*curr], count = data[2*curr+1];
        for(let i = 0; i !== count; ++i) {
          const trii = this.index[offset+i];
          const v0 = new Float32Array(3), v1 = new Float32Array(3), v2 = new Float32Array(3);
          const e0 = new Float32Array(3), e1 = new Float32Array(3);

          this.tri.getTriVertex(trii, 0, v0);
          this.tri.getTriVertex(trii, 1, v1);
          this.tri.getTriVertex(trii, 2, v2);
          sub3(v1, v0, e0); // e0 = v1 - v0
          sub3(v2, v0, e1); // e1 = v2 - v0

          applyMatrix4(toWorld, v0);        // Stores result in v0 so v0 now in world space
          transformDirection(toWorld, e0);
          transformDirection(toWorld, e1);

          const normal = new Float32Array(3);
          cross3(e0, e1, normal); // normal = e0 x e1
          normalise3(normal);

          const tri: Triangle = { v0, e0, e1, normal };
          const plane: Plane = { normal, D: dot3(v0, normal) };

          const dist = dot3(spherePosWs, plane.normal) - plane.D;
          if(Math.abs(dist) > radius) continue;

          let planeP = closestPointOnPlane(plane, spherePosWs, new Float32Array(3));
          if(!pointInTri(tri, planeP)) {
            planeP = closestPointOnTriangle(tri, planeP, new Float32Array(3));
          }

          if(sqDist3(planeP, spherePosWs) <= sqRad) {
            points.push(planeP);
          }
        }
      }
    }
  }
}

const disp = new Float32Array(3);
const sDir = new Float32Array(3);

export function traceRayAgainstSphere (origin: Float32Array, dir: Float32Array, sphere: Float32Array, radius: number): number {
  sub3(sphere, origin, disp); // disp = sphere - origin
  const sqR = radius*radius;
  if(sqLen3(disp) < sqR) return 0;

  const disp_dot_dir = dot3(disp, dir);
  mulScalar3(dir, disp_dot_dir, sDir);
  const discrim = sqR - sqDist3(disp, sDir);
  if(discrim < 0) return -1;
  return disp_dot_dir - Math.sqrt(discrim);
}
