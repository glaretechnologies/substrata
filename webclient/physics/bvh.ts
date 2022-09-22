import * as THREE from '../build/three.module.js'

export type IndexType = Uint32Array | Uint16Array | Uint8Array

export function min3 (curr: Float32Array, currOffset: number, cmp: Float32Array, cmpOffset: number=0): Float32Array {
  curr[currOffset] = Math.min(curr[currOffset], cmp[cmpOffset++]); currOffset++
  curr[currOffset] = Math.min(curr[currOffset], cmp[cmpOffset++]); currOffset++
  curr[currOffset] = Math.min(curr[currOffset], cmp[cmpOffset++]); currOffset++
  return curr
}

export function max3 (curr: Float32Array, currOffset: number, cmp: Float32Array, cmpOffset: number=0): Float32Array {
  curr[currOffset] = Math.max(curr[currOffset], cmp[cmpOffset++]); currOffset++
  curr[currOffset] = Math.max(curr[currOffset], cmp[cmpOffset++]); currOffset++
  curr[currOffset] = Math.max(curr[currOffset], cmp[cmpOffset++]); currOffset++
  return curr
}

export function add3 (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] + rhs[roff]
  out[off+1] = lhs[loff+1] + rhs[roff+1]
  out[off+2] = lhs[loff+2] + rhs[roff+2]
  return out
}

export function sub3 (
  lhs: Float32Array, loff: number,
  rhs: Float32Array, roff: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] - rhs[roff]
  out[off+1] = lhs[loff+1] - rhs[roff+1]
  out[off+2] = lhs[loff+2] - rhs[roff+2]
  return out
}

export function mulScalar3 (
  lhs: Float32Array, loff: number,
  s: number,
  out: Float32Array, off: number
): Float32Array {
  out[off] = lhs[loff] * s
  out[off+1] = lhs[loff+1] * s
  out[off+2] = lhs[loff+2] * s
  return out
}

// The Three.js maths functions don't work well over arrays
export function testAABB (lhs: Float32Array, loff: number, rhs: Float32Array, roff: number): boolean {
  if (lhs[loff+3] < rhs[roff] || lhs[loff] > rhs[roff+3]) return false
  if (lhs[loff+4] < rhs[roff+1] || lhs[loff+1] > rhs[roff+4]) return false
  return !(lhs[loff+5] < rhs[roff+2] || lhs[loff+2] > rhs[roff+5])
}

export function testRayAABB (OP: Float32Array, d: Float32Array, aabb: Float32Array, off: number): [boolean, number] | null {
  return null
}

function range (count: number): Uint32Array {
  const r = new Uint32Array(count)
  for(let i = 0; i !== count; ++i) r[i] = i
  return r
}

/*
This should probably only get the position buffer contents, but Three.js holds on to the whole buffer anyway so
less duplication?
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

    this.pos_offset = offset
    this.vert_stride = stride

    this.centroids = new Float32Array(this.tri_count * 3)
    this.buildCentroids()
  }

  public getTriVertex (tri: number, vertex: number, buf: Float32Array): Float32Array {
    const voff = this.vert_stride * this.index[ 3 * tri + vertex] + this.pos_offset
    buf[0] = this.vertices[voff]; buf[1] = this.vertices[voff+1]; buf[2] = this.vertices[voff+2]
    return buf
  }

  public getCentroid (tri: number, buf: Float32Array): Float32Array {
    const coff = 3 * tri
    buf[0] = this.centroids[coff]; buf[1] = this.centroids[coff+1]; buf[2] = this.centroids[coff+2]
    return buf
  }

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

/*
The BVH nodes are directly stored in two arrays
*/
export default class BVH {
  private readonly aabbBuffer: Float32Array // 6 x floats for each BVN node [minx, miny, minz, maxx, maxy, maxz]
  private readonly dataBuffer: Uint32Array // 2 x uint32s per node = 8 x 4 = 32 bytes [offset, count]

  private readonly tri: Triangles
  private readonly index: Uint32Array
  private readonly maxNodes: number
  private nodeCount: number

  private readonly tmp: Float32Array[]

  public constructor (tri: Triangles) {
    this.tri = tri
    this.index = range(tri.tri_count)
    this.maxNodes = 2 * tri.tri_count - 1 // TODO: Trim after building
    this.aabbBuffer = new Float32Array(this.maxNodes * 6)
    this.dataBuffer = new Uint32Array(this.maxNodes * 2)

    this.tmp = [
      new Float32Array(3),
      new Float32Array(3)
    ]

    // Set the root to all the triangles
    this.dataBuffer[0] = 0; this.dataBuffer[1] = tri.tri_count
    this.nodeCount = 1

    this.computeBound(0)
    this.split(0)
  }

  private computeBound (idx: number) {
    const aabb = this.aabbBuffer, data = this.dataBuffer
    const min_idx = 6 * idx, max_idx = min_idx + 3, data_idx = 2 * idx
    aabb.fill(Number.MAX_VALUE, min_idx, max_idx)
    aabb.fill(-Number.MAX_VALUE, max_idx, max_idx+3)
    const offset = data[data_idx]
    const count = data[data_idx+1]
    const [t0] = this.tmp
    for(let i = 0; i < count; ++i) {
      const tri = this.index[offset+i]
      min3(aabb, min_idx, this.tri.getTriVertex(tri, 0, t0), 0)
      min3(aabb, min_idx, t0, 0)
      min3(aabb, min_idx, this.tri.getTriVertex(tri, 1, t0), 0)
      max3(aabb, max_idx, t0, 0)
      max3(aabb, max_idx, this.tri.getTriVertex(tri, 2, t0), 0)
      max3(aabb, max_idx, t0, 0)
    }
  }

  // Keep the tree somewhat balanced (mean is a better heuristic than median)
  private computeMean (idx: number): [number, number] | null {
    const data_idx = 2 * idx
    const offset = this.dataBuffer[data_idx]
    const count = this.dataBuffer[data_idx+1]

    if(count === 0) return null

    const [t0, t1] = this.tmp

    for(let i = 0; i !== count; ++i) {
      this.tri.getCentroid(this.index[offset + i], t1)
      t0[0] += t1[0]; t0[1] += t1[1]; t0[2] += t1[2]
    }

    const inv = 1./count
    t0[0] *= inv; t0[1] *= inv; t0[2] *= inv

    const buf = this.aabbBuffer
    const min = 6*idx, max = min+3
    t1[0] = (buf[max] - buf[min]); t1[1] = (buf[max+1] - buf[min+1]); t1[2] = buf[max+2] - buf[min+2]
    let longest = Math.max(...t1)
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
    t0[0] = (buf[max] - buf[min]); t0[1] = (buf[max+1] - buf[min+1]); t0[2] = buf[max+2] - buf[min+2]
    let longest = Math.max(...t0)
    for(let i = 0; i !== 3; ++i) if(longest === t0[i]) return [buf[min+i] + t0[i]/2., i]
    return null
  }

  // Organise a range of triangles into left and right bins at splitpoint along axis
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

  private split (idx: number) {
    const data = this.dataBuffer
    const curr_off = 2 * idx
    const offset = data[curr_off], count = data[curr_off+1]
    if(count < 3) return

    const [splitPoint, axis] = this.computeMean(idx)
    //const [splitPoint, axis] = this.computeLongestSplit(idx)
    const cut = this.partition(offset, count, splitPoint, axis)

    // We have left in data[offset, cut] and right in data[cut, offset+count]
    const leftCount = cut - offset, rightCount = count - leftCount
    if(leftCount === 0 || rightCount === 0) return

    const leftNode = this.nodeCount++
    const rightNode = this.nodeCount++

    data[2*leftNode] = offset; data[2*leftNode+1] = leftCount
    data[2*rightNode] = cut; data[2*rightNode+1] = rightCount
    data[curr_off] = leftNode; data[curr_off+1] = 0

    this.computeBound(leftNode); this.computeBound(rightNode)
    this.split(leftNode); this.split(rightNode)
  }

  // Build a debug view of the hierarchy to a certain depth
  public getBVHMesh (leavesOnly: boolean=true): THREE.LineMesh {
    const icount = this.nodeCount * 24
    const buf = new Float32Array(icount)
    const idx = new Uint32Array(icount)

    let used = 0
    let queue = [0]
    while(queue.length > 0 && used < this.nodeCount) {
      const node = queue.shift()
      const offset = this.dataBuffer[2 * node], count = this.dataBuffer[2 * node + 1]
      if(count === 0) {
        queue.push(offset+1, offset)
      } else {
        this.buildAABB(node, buf, idx, 24 * used)
        used += 1
      }
    }

    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3, false))
    geo.setIndex(new THREE.BufferAttribute(idx, 1))

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
    const off_idx = offset/3
    index[i++] = off_idx;   index[i++] = off_idx+1; index[i++] = off_idx+1; index[i++] = off_idx+3
    index[i++] = off_idx;   index[i++] = off_idx+2; index[i++] = off_idx+2; index[i++] = off_idx+3
    index[i++] = off_idx+4; index[i++] = off_idx+5; index[i++] = off_idx+5; index[i++] = off_idx+7
    index[i++] = off_idx+4; index[i++] = off_idx+6; index[i++] = off_idx+6; index[i++] = off_idx+7
    index[i++] = off_idx;   index[i++] = off_idx+4; index[i++] = off_idx+2; index[i++] = off_idx+6
    index[i++] = off_idx+1; index[i++] = off_idx+5; index[i++] = off_idx+3; index[i++] = off_idx+7
  }

  public getRootAABBMesh (): THREE.LineMesh {
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

    return new THREE.LineSegments(geo, new THREE.LineBasicMaterial({color: 'green'}))
  }
}
