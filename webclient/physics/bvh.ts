import * as THREE from '../build/three.module.js'
import { print3 } from '../maths/functions.js'

const EPSILON = .000001

export type IndexType = Uint32Array | Uint16Array | Uint8Array

// Compare current against compare array and store the minimum in current
export function min3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff: number=0): Float32Array {
  curr[coff] = Math.min(curr[coff], cmp[cmpoff]); coff++
  curr[coff] = Math.min(curr[coff], cmp[cmpoff+1]); coff++
  curr[coff] = Math.min(curr[coff], cmp[cmpoff+2]); coff++
  return curr
}

// Compare current against compare array and store the maximum in current
export function max3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff: number=0): Float32Array {
  curr[coff] = Math.max(curr[coff], cmp[cmpoff]); coff++
  curr[coff] = Math.max(curr[coff], cmp[cmpoff+1]); coff++
  curr[coff] = Math.max(curr[coff], cmp[cmpoff+2]); coff++
  return curr
}

// Compare two floating point numbers for equality
export function eq (a: number, b: number, epsilon=EPSILON): boolean {
  return Math.abs(b - a) <= epsilon
}

// Compare two 3-component arrays for equality
export function eq3 (a: Float32Array, b: Float32Array, epsilon=EPSILON): boolean {
  for (let i = 0; i < 3; ++i) {
    if (Math.abs(b[i] - a[i]) > epsilon) return false
  }
  return true
}

// Add 3-component arrays in place
export function add3V (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] + rhs[roff]
  out[off+1] = lhs[loff+1] + rhs[roff+1]
  out[off+2] = lhs[loff+2] + rhs[roff+2]
  return out
}

// Add 3-component vectors
export function add3 (lhs: Float32Array, rhs: Float32Array, out: Float32Array): Float32Array {
  out[0] = lhs[0] + rhs[0]
  out[1] = lhs[1] + rhs[1]
  out[2] = lhs[2] + rhs[2]
  return out
}

// Subtract 3-component arrays in place
export function sub3V (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] - rhs[roff]
  out[off+1] = lhs[loff+1] - rhs[roff+1]
  out[off+2] = lhs[loff+2] - rhs[roff+2]
  return out
}

// Subtract 3-component vectors
export function sub3 (lhs: Float32Array, rhs: Float32Array, out: Float32Array): Float32Array {
  out[0] = lhs[0] - rhs[0]
  out[1] = lhs[1] - rhs[1]
  out[2] = lhs[2] - rhs[2]
  return out
}

// Multiply 3-component by scalar in place
export function mulScalar3V(
  lhs: Float32Array, loff: number,
  s: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] * s
  out[off+1] = lhs[loff+1] * s
  out[off+2] = lhs[loff+2] * s
  return out
}

// Multiply 3-component vectors by scalar
export function mulScalar3 (lhs: Float32Array, s: number, out: Float32Array): Float32Array {
  out[0] = lhs[0] * s
  out[1] = lhs[1] * s
  out[2] = lhs[2] * s
  return out
}

// Compute 3-component cross product
export function cross3 (u: Float32Array, v: Float32Array, out: Float32Array): Float32Array {
  out[0] = u[1] * v[2] - u[2] * v[1]
  out[1] = u[2] * v[0] - u[0] * v[2]
  out[2] = u[0] * v[1] - u[1] * v[0]
  return out
}

// Compute 3-component dot product
export function dot3 (u: Float32Array, v: Float32Array): number {
  return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]
}

// Test AABBs for intersection in place
export function testAABB (lhs: Float32Array, loff: number, rhs: Float32Array, roff: number): boolean {
  if (lhs[loff+3] < rhs[roff] || lhs[loff] > rhs[roff+3]) return false
  if (lhs[loff+4] < rhs[roff+1] || lhs[loff+1] > rhs[roff+4]) return false
  return !(lhs[loff+5] < rhs[roff+2] || lhs[loff+2] > rhs[roff+5])
}

// Test Ray against AABB
export function testRayAABB (OP: Float32Array, d: Float32Array, aabb: Float32Array, off: number=0): [boolean, number] | null {
  let t_min = 0
  let t_max = Number.MAX_VALUE
  const min = aabb.slice(off, off+3)
  const max = aabb.slice(off+3, off+6)

  //print3('min', min, 'max', max)

  for (let i = 0; i < 3; ++i) {
    if(Math.abs(d[i]) < EPSILON) {
      if (OP[i] < min[i] || OP[i] > max[i]) return [false, Number.POSITIVE_INFINITY]
    } else {
      let invD = 1. / d[i]
      let t0 = (min[i] - OP[i]) * invD
      let t1 = (max[i] - OP[i]) * invD
      if (t0 > t1) {
        const tmp = t0; t0 = t1; t1 = tmp
      }
      t_min = Math.max(t_min, t0)
      t_max = Math.min(t_max, t1)

      if(t_min > t_max) return [false, Number.POSITIVE_INFINITY]
    }
  }

  return [true, t_min]
}

// Generate a sequence of numbers from offset to count in an Uint32Array
function range (count: number, offset: number=0): Uint32Array {
  const r = new Uint32Array(count)
  if(offset === 0) {
    for(let i = 0; i !== count; ++i) r[i] = i
  } else {
    for(let i = 0; i !== count; ++i) r[i] = i + offset
  }

  return r
}

/*
The triangles structure is built directly from the interleaved mesh buffer and associated index buffer.  The data is
therefore directly derived from the rendering data.
*/
export class Triangles {
  public vertices: Float32Array
  public centroids: Float32Array
  public index: IndexType
  public readonly pos_offset: number
  public readonly tri_count: number
  public readonly vert_num: number
  public readonly vert_stride: number

  // Initialised with the existing interleaved buffer so that we don't need to copy any data
  public constructor (vertices: Float32Array, index: IndexType, offset: number, stride: number) {
    this.vertices = vertices
    this.tri_count = Math.floor(index.length / 3)
    if(index.length % 3 !== 0) {
      console.warn('index length not a multiple of 3', this.tri_count)
    }

    this.vert_num = Math.floor(vertices.length / stride)
    if(vertices.length % stride !== 0) {
      console.warn('vertices length not a multiple of stride', this.vert_num)
    }

    this.index = index
    this.pos_offset = offset // The offset of the position attribute in the interleaved buffer
    this.vert_stride = stride // The stride for the vertex

    this.centroids = new Float32Array(this.tri_count * 3)
    this.buildCentroids()
  }

  // Get the Vertex id (vertex) for the triangle id (tri) in the 3-component output
  public getTriVertex (tri: number, vertex: number, output: Float32Array): Float32Array {
    const voff = this.vert_stride * this.index[ 3 * tri + vertex] + this.pos_offset
    output[0] = this.vertices[voff]; output[1] = this.vertices[voff+1]; output[2] = this.vertices[voff+2]
    return output
  }

  // Get the centroid for triangle id (tri) in the 3-component output
  public getCentroid (tri: number, output: Float32Array): Float32Array {
    const coff = 3 * tri
    output[0] = this.centroids[coff]; output[1] = this.centroids[coff+1]; output[2] = this.centroids[coff+2]
    return output
  }

  // Temporaries that we create once and avoid recreating each time we call the intersectTriangle function.
  // These instances take time to create so we prefer destructuring them into the function (which is very fast).
  private tmp: Float32Array[] = [
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3),
    new Float32Array(3), new Float32Array(3)
  ]

  // Moeller-Trumbore ray triangle intersection
  public intersectTriangle (origin: Float32Array, dir: Float32Array, triIndex: number): number | null {
    // Destructure the array into the named variables we'll use in the function
    const [v0, v1, v2, e0, e1, h, s, q] = this.tmp

    this.getTriVertex(triIndex, 0, v0)
    this.getTriVertex(triIndex, 1, v1)
    this.getTriVertex(triIndex, 2, v2)

    sub3(v1, v0, e0)
    sub3(v2, v0, e1)
    cross3(dir, e1, h)

    const a = dot3(e0, h)
    if (eq(a, 0, EPSILON)) return null

    const f = 1.0 / a
    sub3(origin, v0, s)
    const u = f * dot3(s, h)
    if (u < 0 || u > 1) return null

    cross3(s, e0, q)
    const v = f * dot3(dir, q)
    if (v < 0 || u + v > 1) return null

    const t = f * dot3(e1, q)
    return t > EPSILON ? t : null
  }

  // Traverse the collection of triangles and build the centroids
  private buildCentroids (): void {
    for(let tri = 0, idx = 0; tri < this.tri_count; ++tri, idx += 3) {
      const A = this.vert_stride * this.index[idx] + this.pos_offset
      const B = this.vert_stride * this.index[idx+1] + this.pos_offset
      const C = this.vert_stride * this.index[idx+2] + this.pos_offset

      for(let i = 0; i < 3; ++i) {
        this.centroids[idx+i] = (this.vertices[A+i] + this.vertices[B+i] + this.vertices[C+i])/3
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
  private readonly aabbBuffer: Float32Array // 6 x floats for each BVN node [minx, miny, minz, maxx, maxy, maxz]
  // Stores data per node in 2 consecutive uint32s - offset and count
  // If count is 0 then offset stores the left child of the branch, the rightChild = (leftChild + 1)
  // If count is > 0 then offset refers to the consecutive triangle indices stored in the index array.
  private readonly dataBuffer: Uint32Array // 2 x uint32s per node [offset, count]
  // Total memory usage per node = (6 + 2) x 4 = 32 bytes

  private readonly tri: Triangles
  private readonly index: Uint32Array // The triangle index stored in consecutive ranges in the nodes
  private readonly maxNodes: number
  private nodeCount: number // Number of used nodes

  private heuristic_fnc: heuristicFunction

  // Temporaries used in functions to avoid creating new arrays on each call
  private readonly tmp = [
    new Float32Array(3),
    new Float32Array(3),
    new Float32Array(3)
  ]

  public constructor (tri: Triangles) {
    this.tri = tri
    this.index = range(tri.tri_count)
    this.maxNodes = 2 * tri.tri_count - 1 // TODO: Trim after building
    this.aabbBuffer = new Float32Array(this.maxNodes * 6)
    this.dataBuffer = new Uint32Array(this.maxNodes * 2)

    // Set the default heuristic for split cost calculation
    this.heuristic_fnc = this.computeLongestSplit

    // Set the root node with the all the triangles...
    // Set offset = 0 and count = tri.tri_count for node 0
    this.dataBuffer[0] = 0; this.dataBuffer[1] = tri.tri_count
    this.nodeCount = 1

    // Compute bound of node 0 and recursively split node 0
    this.computeAABB(0)
    this.split(0)
  }

  // Boolean Result
  public testRayRoot (origin: Float32Array, dir: Float32Array): boolean {
    //print3('origin', origin, 'dir', dir)
    const query = testRayAABB(origin, dir, this.aabbBuffer)
    return query[0]
  }

  // Returns intersection and distance along ray TODO
  private intersectRayRoot (origin: THREE.Vector3, dir: THREE.Vector3, P: THREE.Vector3): number {
    return 0
  }

  // Returns the index of the BVH node, and triangle of intersection with the ray
  public testRayLeaf (origin: Float32Array, dir: Float32Array): [number, number] { // [nodeIdx, triIdx]
    let t_min = Number.MAX_VALUE
    let idx = -1
    let tri_idx = -1
    let q = [0]
    while (q.length > 0) {
      const curr = q.shift()
      const isLeaf = this.dataBuffer[2*curr+1] > 0
      const [test, t] = testRayAABB(origin, dir, this.aabbBuffer, 6 * curr)
      if (test) {
        if (isLeaf) {
          const offset = this.dataBuffer[2*curr], count = this.dataBuffer[2*curr+1]
          for(let i = 0; i < count; ++i) {
            const tri = this.index[offset+i]
            const t = this.tri.intersectTriangle(origin, dir, tri)
            if(t != null && t < t_min) {
              t_min = t
              tri_idx = tri
              idx = curr
            }
          }

        }
        if(!isLeaf) {
          q.push(this.dataBuffer[2*curr], this.dataBuffer[2*curr]+1)
        }
      }
    }

    /*
    let triIdx = -1
    if(idx !== -1) {
      const offset = this.dataBuffer[2*idx], count = this.dataBuffer[2*idx+1]
      let t_min = Number.MAX_VALUE
      for(let i = 0; i < count; ++i) {
        const tri = this.index[offset+i]
        console.log('testing:', tri)
        const t = this.tri.intersectTriangle(origin, dir, tri)
        if(t != null && t < t_min) {
          t_min = t
          triIdx = tri
        }
      }
    }
    */
    return [idx, tri_idx]
  }

  // Compute the AABB for the input node and store the result in the AABB buffer
  private computeAABB (nodeIdx: number) {
    const aabb = this.aabbBuffer, data = this.dataBuffer
    const min_idx = 6 * nodeIdx, max_idx = min_idx + 3, data_idx = 2 * nodeIdx

    aabb.fill(Number.MAX_VALUE, min_idx, max_idx)
    aabb.fill(-Number.MAX_VALUE, max_idx, max_idx+3)

    const offset = data[data_idx]
    const count = data[data_idx+1]
    const [t0] = this.tmp

    for(let i = 0; i < count; ++i) {
      const tri = this.index[offset+i]
      // Read triangle tri, vertex 0 into t0
      this.tri.getTriVertex(tri, 0, t0)
      min3(aabb, min_idx, t0, 0)
      max3(aabb, max_idx, t0, 0)
      // Read triangle tri, vertex 1 into t0
      this.tri.getTriVertex(tri, 1, t0)
      min3(aabb, min_idx, t0, 0)
      max3(aabb, max_idx, t0, 0)
      // Read triangle tri, vertex 2 into t0
      this.tri.getTriVertex(tri, 2, t0)
      min3(aabb, min_idx, t0, 0)
      max3(aabb, max_idx, t0, 0)
    }
  }

  // Keep the tree somewhat balanced (mean is a better heuristic than median)
  private computeMean (nodeIdx: number): [number, number] | null {
    const data_idx = 2 * nodeIdx
    const offset = this.dataBuffer[data_idx]
    const count = this.dataBuffer[data_idx+1]

    if(count === 0) return null

    const [t0, t1] = this.tmp
    t0.fill(0)

    for(let i = 0; i !== count; ++i) {
      // Read the triangle centroid into t1
      this.tri.getCentroid(this.index[offset + i], t1)
      // Accumulate the centroids
      t0[0] += t1[0]; t0[1] += t1[1]; t0[2] += t1[2]
    }

    // Average
    const inv = 1./count
    t0[0] *= inv; t0[1] *= inv; t0[2] *= inv

    const buf = this.aabbBuffer
    const min = 6*nodeIdx, max = min+3
    // Compute the max extents
    t1[0] = (buf[max] - buf[min]); t1[1] = (buf[max+1] - buf[min+1]); t1[2] = buf[max+2] - buf[min+2]
    let longest = Math.max(...t1)
    // Choose the longest axis for the split
    for(let i = 0; i !== 3; ++i) if(longest == t1[i]) return [t0[i], i]

    return null
  }

  // Try the SAH.  Probably too slow, and we should prefer building speed over query speed as we will do few queries
  // per frame
  private computeSAH (idx: number): [number, number] | null {
    return null
  }

  // Compute split of parent AABB on longest axis
  private computeLongestSplit (idx: number): [number, number] | null {
    const [t0] = this.tmp
    const buf = this.aabbBuffer
    const min = 6*idx, max = min+3
    // Compute extents of aabb
    t0[0] = (buf[max] - buf[min]); t0[1] = (buf[max+1] - buf[min+1]); t0[2] = buf[max+2] - buf[min+2]
    let longest = Math.max(...t0)
    // Split on longest axis and compute centre point as split point
    for(let i = 0; i !== 3; ++i) if(longest === t0[i]) return [buf[min+i] + t0[i]/2., i]
    return null
  }

  // Organise a range of triangles into left and right bins at splitPoint along axis
  private partition (offset: number, count: number, splitPoint: number, axis: number): number {
    const [centroid] = this.tmp

    let left = offset, right = offset + count - 1
    while (left <= right) {
      const tri = this.index[left]
      this.tri.getCentroid(tri, centroid)
      if(centroid[axis] < splitPoint) left += 1
      else { // Swap to the front of the right
        this.index[left] = this.index[right]
        this.index[right] = tri
        right -= 1
      }
    }
    return left
  }

  public set heuristic (fnc: (nodeIdx: number) => [number, number]) {
    this.heuristic_fnc = fnc
  }

  // Split an input node (if possible) based on selected heuristic
  private split (nodeIdx: number) {
    const data = this.dataBuffer
    const curr_off = 2 * nodeIdx
    const offset = data[curr_off], count = data[curr_off+1]
    if(count < 2) return

    const [splitPoint, axis] = this.heuristic_fnc(nodeIdx)
    const cut = this.partition(offset, count, splitPoint, axis)

    // We have left in data[offset, cut] and right in data[cut, offset+count]
    const leftCount = cut - offset, rightCount = count - leftCount
    if(leftCount === 0 || rightCount === 0) return

    const leftNode = this.nodeCount++
    const rightNode = this.nodeCount++

    // Store the left and right triangle collections in the two consecutive nodes
    data[2*leftNode] = offset; data[2*leftNode+1] = leftCount
    data[2*rightNode] = cut; data[2*rightNode+1] = rightCount
    // Set the split node to the index of the left child & count to 0
    data[curr_off] = leftNode; data[curr_off+1] = 0

    // Compute the bound for each node and try to split each
    this.computeAABB(leftNode); this.computeAABB(rightNode)
    this.split(leftNode); this.split(rightNode)
  }

  // Build a visualisation of a leaves-only BVH tree
  public getBVHMesh (): THREE.LineSegments {
    const icount = this.nodeCount * 24 // Max possible nodes, TODO optimise this
    const buf = new Float32Array(icount)
    const idx = new Uint32Array(icount)

    let used = 0
    // Use a queue rather than recursion to traverse the tree.
    let queue = [0]
    while(queue.length > 0 && used < this.nodeCount) {
      const node = queue.shift()
      const offset = this.dataBuffer[2 * node], count = this.dataBuffer[2 * node + 1]
      if(count === 0) { // If count is zero, this is a branch so push to two consecutive child nodes
        queue.push(offset, offset+1)
      } else {
        // Build an AABB box into the mesh
        this.buildAABB(node, buf, idx, 24 * used)
        used += 1
      }
    }

    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false))
    geo.setIndex(new THREE.BufferAttribute(idx, 1))

    // We only use the used number of leaf nodes thus wasting some memory
    // TODO: to be optimised
    geo.setDrawRange(0, used*24)

    return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'green'}))
  }

  private buildAABB (idx: number, vertices: Float32Array, index: Uint32Array, offset: number) {
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(6*idx, 6*(idx+1))
    let i = offset

    vertices[i++] = minx; vertices[i++] = miny; vertices[i++] = maxz // 0
    vertices[i++] = minx; vertices[i++] = miny; vertices[i++] = minz // 1
    vertices[i++] = minx; vertices[i++] = maxy; vertices[i++] = maxz // 2
    vertices[i++] = minx; vertices[i++] = maxy; vertices[i++] = minz // 3
    vertices[i++] = maxx; vertices[i++] = miny; vertices[i++] = maxz // 4
    vertices[i++] = maxx; vertices[i++] = miny; vertices[i++] = minz // 5
    vertices[i++] = maxx; vertices[i++] = maxy; vertices[i++] = maxz // 6
    vertices[i++] = maxx; vertices[i++] = maxy; vertices[i++] = minz // 7

    i = offset
    const off_idx = offset / 3
    index[i++] = off_idx;   index[i++] = off_idx+1; index[i++] = off_idx+1; index[i++] = off_idx+3
    index[i++] = off_idx;   index[i++] = off_idx+2; index[i++] = off_idx+2; index[i++] = off_idx+3
    index[i++] = off_idx+4; index[i++] = off_idx+5; index[i++] = off_idx+5; index[i++] = off_idx+7
    index[i++] = off_idx+4; index[i++] = off_idx+6; index[i++] = off_idx+6; index[i++] = off_idx+7
    index[i++] = off_idx;   index[i++] = off_idx+4; index[i++] = off_idx+2; index[i++] = off_idx+6
    index[i++] = off_idx+1; index[i++] = off_idx+5; index[i++] = off_idx+3; index[i++] = off_idx+7
  }

  // Build an AABB mesh on the root of the BVH
  public getRootAABBMesh (): THREE.LineSegments {
    const buf = new Float32Array(24)
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(0, 6)

    let i = 0
    buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz // 0
    buf[i++] = minx; buf[i++] = miny; buf[i++] = minz // 1
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz // 2
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz // 3
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz // 4
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz // 5
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz // 6
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz // 7

    const idx = new Uint8Array(24)

    i = 0
    // left
    idx[i++] = 0; idx[i++] = 1; idx[i++] = 1; idx[i++] = 3
    idx[i++] = 0; idx[i++] = 2; idx[i++] = 2; idx[i++] = 3
    // right
    idx[i++] = 4; idx[i++] = 5; idx[i++] = 5; idx[i++] = 7
    idx[i++] = 4; idx[i++] = 6; idx[i++] = 6; idx[i++] = 7
    // front
    idx[i++] = 0; idx[i++] = 4; idx[i++] = 2; idx[i++] = 6
    // back
    idx[i++] = 1; idx[i++] = 5; idx[i++] = 3; idx[i++] = 7

    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false))
    geo.setIndex(new THREE.BufferAttribute(idx, 1))

    return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'red'}))
  }

  // Update a single AABB mesh with updated data
  public updateAABBMesh (mesh: THREE.LineMesh, idx: number): void {
    const attr = mesh.geometry.getAttribute('position')
    if(!attr) return

    const buf = attr.array
    const [minx, miny, minz, maxx, maxy, maxz] = this.aabbBuffer.slice(6*idx, 6*(idx+1))
    let i = 0
    buf[i++] = minx; buf[i++] = miny; buf[i++] = maxz
    buf[i++] = minx; buf[i++] = miny; buf[i++] = minz
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = maxz
    buf[i++] = minx; buf[i++] = maxy; buf[i++] = minz
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = maxz
    buf[i++] = maxx; buf[i++] = miny; buf[i++] = minz
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = maxz
    buf[i++] = maxx; buf[i++] = maxy; buf[i++] = minz

    attr.needsUpdate = true
  }

  // Create a triangle highlighter to highlight the outline of triangles on the mesh represented by this BVH
  public getTriangleHighlighter (): THREE.LineLoop {
    const buffer = new Float32Array(9)
    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.BufferAttribute(buffer, 3, false))
    const mat = new THREE.LineBasicMaterial({color: 'blue', depthTest: false})
    return new THREE.LineLoop(geo, mat)
  }

  // Update the triangle highlighter to the triangle represented by triangleIdx
  public updateTriangleHighlighter (triangleIdx: number, mesh: THREE.LineLoop): void {
    const attr = mesh.geometry.getAttribute('position')
    if(!attr) return

    const buffer = attr.array
    const [v0, v1, v2] = this.tmp
    this.tri.getTriVertex(triangleIdx, 0, v0)
    this.tri.getTriVertex(triangleIdx, 1, v1)
    this.tri.getTriVertex(triangleIdx, 2, v2)
    buffer.set(v0, 0); buffer.set(v1, 3); buffer.set(v2, 6)
    attr.needsUpdate = true
  }

  // Debug code for testing assignment and other properties
  traverse () {
    const back = this.nodeCount
    const data = this.dataBuffer
    const aabb = this.aabbBuffer

    // If we walk from the back, do we get all triangles?
    for (let i = back - 1; i >= 0; --i) {
      const idx = 2*i
      console.log('node:', i, 'offset', data[idx], 'count:', data[idx+1], 'isLeaf:', data[idx+1] !== 0)
    }

    // Are all triangles reachable...?
    let ids = []
    const q = [0]
    while (q.length > 0) {
      const curr = q.shift()
      const offset = data[2*curr], count = data[2*curr+1]
      if(count === 0) {
        q.push(offset, offset+1)
      } else {
        const tris = Array.from(range(count, offset))
        console.log('tris:', tris)
        print3('min', aabb.slice(6*curr, 6*curr + 3), 'max', aabb.slice(6*curr+3, 6*curr+6))
        ids = [...ids, ...tris]
      }
    }

    const output = new Set(ids)
    console.log('output', output)
  }
}
