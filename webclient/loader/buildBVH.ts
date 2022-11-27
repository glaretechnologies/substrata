/*=====================================================================
buildBVH.ts
---------------
Copyright Glare Technologies Limited 2022 -

The extracted dependencies for creating a bvh.
=====================================================================*/

// Generate a sequence of numbers from offset to count in an Uint32Array
function range (count: number, offset=0): Uint32Array {
	const r = new Uint32Array(count);
	if(offset === 0) {
		for(let i = 0; i !== count; ++i) r[i] = i;
	} else {
		for(let i = 0; i !== count; ++i) r[i] = i + offset;
	}

	return r;
}

// Compare current against compare array and store the minimum in current
function min3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff=0): Float32Array {
	curr[coff] = Math.min(curr[coff], cmp[cmpoff]); coff++;
	curr[coff] = Math.min(curr[coff], cmp[cmpoff+1]); coff++;
	curr[coff] = Math.min(curr[coff], cmp[cmpoff+2]); coff++;
	return curr;
}

// Compare current against compare array and store the maximum in current
function max3 (curr: Float32Array, coff: number, cmp: Float32Array, cmpoff=0): Float32Array {
	curr[coff] = Math.max(curr[coff], cmp[cmpoff]); coff++;
	curr[coff] = Math.max(curr[coff], cmp[cmpoff+1]); coff++;
	curr[coff] = Math.max(curr[coff], cmp[cmpoff+2]); coff++;
	return curr;
}

type IndexType = Uint32Array | Uint16Array | Uint8Array

enum IntIndex {
  UINT8 = 0,
  UINT16 = 1,
  UINT32 = 2
}

// Creates an index buffer based on the input indexType (getIndexType) used for transferring memory between main / worker threads
function createIndex (indexType: IntIndex, buf: ArrayBuffer): IndexType | null {
	if(indexType === IntIndex.UINT8) return new Uint8Array(buf);
	else if(indexType === IntIndex.UINT16) return new Uint16Array(buf);
	else if(indexType === IntIndex.UINT32) return	new Uint32Array(buf);
}

function copyIndex (index: IndexType): IndexType | null {
	if(index instanceof Uint8Array) return new Uint8Array(index);
	else if(index instanceof Uint16Array) return new Uint16Array(index);
	else if(index instanceof Uint32Array) return new Uint32Array(index);
	return null;
}

class Triangles {
	public vertices: Float32Array;
	public centroids?: Float32Array;
	public index: IndexType;
	//public readonly pos_offset: number; // pos is always == 0
	public readonly tri_count: number;
	public readonly vert_num: number;
	public readonly vert_stride: number;

	// Builds an optimised Triangles structure from an interleaved buffer to reduce data transmission from main thread to
	// worker thread
	public static extractTriangles (vertices: Float32Array, index: IndexType, stride: number): Triangles {
		const vertexCount = Math.floor(vertices.length / stride);
		const vbuffer = new Float32Array(3 * vertexCount);
		for(let i = 0; i !== vertexCount; ++i) {
			vbuffer[3*i] = vertices[stride*i];
			vbuffer[3*i+1] = vertices[stride*i+1];
			vbuffer[3*i+2] = vertices[stride*i+2];
		}

		// Create a copy of the Index Buffer because we transfer it to a worker
		return new Triangles(vbuffer, copyIndex(index), 3);
	}

	// Initialised with the existing interleaved buffer so that we don't need to copy any data
	public constructor (vertices: Float32Array, index: IndexType, stride: number) {
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
		//this.pos_offset = offset; // The offset of the position attribute in the interleaved buffer (always == 0)
		this.vert_stride = stride; // The stride for the vertex
	}

	// Get the Vertex id (vertex) for the triangle id (tri) in the 3-component output
	public getTriVertex (tri: number, vertex: number, output: Float32Array): Float32Array {
		const voff = this.vert_stride * this.index[3 * tri + vertex];
		output[0] = this.vertices[voff]; output[1] = this.vertices[voff+1]; output[2] = this.vertices[voff+2];
		return output;
	}

	// Get the centroid for triangle id (tri) in the 3-component output
	public getCentroid (tri: number, output: Float32Array): Float32Array {
		const coff = 3 * tri;
		output[0] = this.centroids[coff]; output[1] = this.centroids[coff+1]; output[2] = this.centroids[coff+2];
		return output;
	}

	// Traverse the collection of triangles and build the centroids
	public buildCentroids (): void {
		if(!this.centroids) this.centroids = new Float32Array(this.tri_count * 3);

		for(let tri = 0, idx = 0; tri < this.tri_count; ++tri, idx += 3) {
			const A = this.vert_stride * this.index[idx];
			const B = this.vert_stride * this.index[idx+1];
			const C = this.vert_stride * this.index[idx+2];

			for(let i = 0; i < 3; ++i) {
				this.centroids[idx+i] = (this.vertices[A+i] + this.vertices[B+i] + this.vertices[C+i])/3;
			}
		}
	}
}

interface BVHData {
  index: Uint32Array;
  nodeCount: number;
  aabbBuffer: Float32Array
  dataBuffer: Uint32Array
}

/*
The BVH nodes are directly stored in two arrays
*/
class BVH {
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

	//private heuristic_fnc: heuristicFunction;

	// Temporaries used in functions to avoid creating new arrays on each call
	private readonly tmp = [
		new Float32Array(3),
		new Float32Array(3),
		new Float32Array(3)
	];

	public constructor (tri: Triangles, copy?: BVHData) { // Use BVHData when copying from worker
		this.tri = tri;
		if(copy != null) { // Transfer of internal structures from worker
			this.index = copy.index;
			this.maxNodes = copy.nodeCount;
			this.nodeCount = copy.nodeCount;
			this.aabbBuffer = copy.aabbBuffer;
			this.dataBuffer = copy.dataBuffer;
		} else {
			this.index = range(tri.tri_count);
			this.maxNodes = 2 * tri.tri_count - 1; // TODO: Trim after building
			this.aabbBuffer = new Float32Array(this.maxNodes * 6);
			this.dataBuffer = new Uint32Array(this.maxNodes * 2);
			if(!tri.centroids) tri.buildCentroids();
			// this.heuristic_fnc = this.computeMean; // Set the default heuristic for split cost calculation

			// Set the root node with the all the triangles...
			// Set offset = 0 and count = tri.tri_count for node 0
			this.dataBuffer[0] = 0; this.dataBuffer[1] = tri.tri_count;
			this.nodeCount = 1;

			// Compute bound of node 0 and recursively split node 0
			this.computeAABB(0);
			this.split(0);
		}
	}

	public get bvhData(): BVHData {
		return {
			index: this.index,
			nodeCount: this.nodeCount,
			aabbBuffer: new Float32Array(this.aabbBuffer.slice(0, 6*this.nodeCount)),
			dataBuffer: new Uint32Array(this.dataBuffer.slice(0, 2*this.nodeCount))
		};
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

	// Split an input node (if possible) based on selected heuristic
	private split (nodeIdx: number) {
		const data = this.dataBuffer;
		const curr_off = 2 * nodeIdx;
		const offset = data[curr_off], count = data[curr_off+1];
		if(count <= this.maxNodeSize) return; // Don't split further, node is already small enough

		const [splitPoint, axis] = this.computeMean(nodeIdx); // this.heuristic_fnc(nodeIdx);
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
}

interface WorkerParameters {
  key: string; // The model_url or voxel id of the mesh
  indexCount: number; // The number of elements in the indexBuf ArrayBuffer
  indexType: number; // The type of index buffer: 0 = Uint8Array, 1 = Uint16Array, 2 = Uint32Array
  indexBuf: ArrayBuffer; // The index ArrayBuffer we transfer from main to worker
  stride: number; // The stride of the vertexBuf, now typically 3
  vertexCount: number; // The number of vertices in the vertexBuf ArrayBuffer
  vertexBuf: ArrayBuffer; // The vertices ArrayBuffer
}

interface WorkerResult {
  key: string;
  indexCount: number;
  indexType: number;
  indexBuf: ArrayBuffer;
  stride: number;
  vertexCount: number;
  vertexBuf: ArrayBuffer;

  // This data is the returned BVH data
  bvhIndexBuf: ArrayBuffer // Uint32Array type
  nodeCount: number; // Total number of nodes in BVH
  aabbBuffer: ArrayBuffer // Float32Array(6 * nodeCount)
  dataBuffer: ArrayBuffer // Uint32Array(2 * nodeCount)
}

function createTriangles (obj: WorkerParameters | WorkerResult) {
	const triIndex = createIndex(obj.indexType as IntIndex, obj.indexBuf);
	const vertices = new Float32Array(obj.vertexBuf);
	return new Triangles(vertices, triIndex, obj.stride);
}
