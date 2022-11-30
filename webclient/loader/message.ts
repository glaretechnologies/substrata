/*=====================================================================
message.ts
---------------
Copyright Glare Technologies Limited 2022 -

Contains the interfaces defining the worker request and response for
mesh building.
=====================================================================*/

export const BMESH_TYPE = 1;
export const VOXEL_TYPE = 2;

export interface VertAttribute {
  type: number;
  component_type: number;
  offset_B: number;
}

export interface Voxels {
  uid: bigint // The id of the world_ob to which the compressedVoxels belong
  compressedVoxels: ArrayBuffer // If specified, represents the input data to the voxel decompression & loading code.
  mats_transparent: boolean[]; // Which materials are transparent
  ob_lod_level: number;
  model_lod_level: number;
}

// Input Task Description
export interface MeshLoaderRequest {
  pos: Float32Array; // The centroid of the AABB of the mesh
  sizeFactor: number // Object Scale
  bmesh?: string // If specified, the URL string
  voxels?: Voxels // The input voxels structure containing the compressed buffer and other dependent data
}

export interface BMesh {
  url: string // The url at which the mesh definition resides
  groupsBuffer: ArrayBuffer // The material groups (3 x uint32) - Uint32Array
  indexType: number // Uint8 = 0, Uint16 = 1, Uint32 = 2
  indexBuffer: ArrayBuffer // May be Uint8Array, Uint16Array, or Uint32Array
  interleaved: ArrayBuffer // Float32Array interleaved
  interleavedStride: number // Vertex stride in interleaved
  attributes: Array<VertAttribute> // The attributes packed into the interleaved buffer
}

export interface VoxelMesh {
  uid: bigint // The world_ob id
  groupsBuffer: ArrayBuffer // The material groups (3 x uint32) - Actual Type is Uint32Array
  positionBuffer: ArrayBuffer // The position buffer (3 x float32) - Float32Array
  indexBuffer: ArrayBuffer // The index buffer - Uint32Array
  subsample_factor: number
  ob_lod_level: number;
  model_lod_level: number;
}

export interface BVHTransfer {
  bvhIndexBuffer: ArrayBuffer // Index with type defined by indexType (always Uint32)
  nodeCount: number, // Number of nodes in the BVH
  aabbBuffer: ArrayBuffer // Float32Array
  dataBuffer: ArrayBuffer // Uint32Array
  indexType: number // Type of index 0 = Uint8, 1 = Uint16, 2 = Uint32
  triIndexBuffer: ArrayBuffer // The index of the triangle structure (indexType)
  triPositionBuffer: ArrayBuffer // Float32Array of positions for triangle structure
}

export interface LoaderError {
  type: number; // bmesh = 1, voxel = 2 (CONSTANTS DEFINED ABOVE)
  message: string // description of the error
  url?: string // In the case of a bmesh load error, we need to return the URL so we can remove it from the download set
}

// Should we move the BVH construction here - makes sense to do it here as we have all the necessary data?
export interface MeshLoaderResponse {
  pos: Float32Array; // The centroid of the object
  sizeFactor: number; // Object scale
  bMesh?: BMesh // Return an object mesh
  voxelMesh?: VoxelMesh // Return a voxelMesh
  bvh?: BVHTransfer // Return the BVH data (loaded in this worker)
  error?: LoaderError // Returned if some error occurs during the load
}
