/*=====================================================================
MeshLoaderWorker.ts
---------------
Copyright Glare Technologies Limited 2022 -

NOTE: The Worker cannot import any code, so ALL building code has to
be included below.  This first version is for Chrome only so it's
easier to debug before we harden it for all browsers.
=====================================================================*/

import { BMESH_TYPE, MeshLoaderRequest, VOXEL_TYPE, Voxels } from './message.js';
import { loadBatchedMesh } from '../bmeshloading.js';
import { makeMeshForVoxelGroup, VoxelMeshData } from '../voxelloading.js';
import { getIndexType } from '../physics/bvh.js';

function processBMeshRequest (url: string, pos: Float32Array, sizeFactor: number): void {
	const encoded_url = encodeURIComponent(url);
	fetch('/resource/' + encoded_url).then(resp => {
		if(resp.status >= 200 && resp.status < 300) {
			return resp.arrayBuffer();
		} else {
			self.postMessage({
				error: {
					type: BMESH_TYPE,
					message: `Fetch on url ${url} failed with error: ${resp.status}`,
					url
				}
			});
			return;
		}
	}).then((buffer: ArrayBuffer) => {
		if(buffer == null) return;

		// Load the batched mesh (decompress etc.)
		const {
			groupsBuffer, indexType, indexBuffer, interleaved, interleavedStride, attributes, bvh
		} = loadBatchedMesh(buffer);

		// Send a response back to the main thread, with the built data.
		const bvhData = bvh.bvhData;

		const bvhTransfer = {
			bvhIndexBuffer: bvhData.index.buffer,
			nodeCount: bvhData.nodeCount,
			aabbBuffer: bvhData.aabbBuffer.buffer,
			dataBuffer: bvhData.dataBuffer.buffer,
			indexType: getIndexType(bvh.tri.index),
			triIndexBuffer: bvh.tri.index.buffer,
			triPositionBuffer: bvh.tri.vertices.buffer
		};

		const resp = {
			pos,
			sizeFactor,
			bMesh: {
				url,
				groupsBuffer,
				indexType,
				indexBuffer,
				interleaved,
				interleavedStride,
				attributes
			},
			bvh: bvhTransfer
		};
		// @ts-expect-error - incorrect interface reported by TS, postMessage should be (message, transferList)
		self.postMessage(resp,[
			resp.bMesh.groupsBuffer,	// Transferred Buffers
			resp.bMesh.interleaved,
			resp.bMesh.indexBuffer,
			resp.bvh.bvhIndexBuffer,
			resp.bvh.aabbBuffer,
			resp.bvh.dataBuffer,
			resp.bvh.triIndexBuffer,
			resp.bvh.triPositionBuffer
		]);
	}).catch(err => {
		self.postMessage({
			error: {
				type: BMESH_TYPE,
				message: `bmesh ${url} failed to load with ${err}`,
				url
			}
		});
	});
}

function processVoxelMeshRequest (voxels: Voxels, pos: Float32Array, sizeFactor: number): void {
	const { uid, model_lod_level, ob_lod_level } = voxels;

	// Build the voxel mesh
	let def: VoxelMeshData;
	try {
		def = makeMeshForVoxelGroup(voxels.compressedVoxels, voxels.model_lod_level, voxels.mats_transparent);
	} catch(err) {
		self.postMessage({
			error: {
				type: VOXEL_TYPE,
				message: `voxel mesh ${voxels.uid} failed to load with ${err}`
			}
		});
		return;
	}

	// Send a response back to the main thread, with the built data.
	const bvhData = def.bvh.bvhData;
	const bvhTransfer = {
		bvhIndexBuffer: bvhData.index.buffer,
		nodeCount: bvhData.nodeCount,
		aabbBuffer: bvhData.aabbBuffer.buffer,
		dataBuffer: bvhData.dataBuffer.buffer,
		indexType: getIndexType(def.bvh.tri.index),
		triIndexBuffer: def.bvh.tri.index.buffer,
		triPositionBuffer: def.bvh.tri.vertices.buffer
	};

	self.postMessage({
		pos,
		sizeFactor,
		voxelMesh: {
			uid,
			groupsBuffer: def.groupsBuffer,
			positionBuffer: def.positionBuffer,
			indexBuffer: def.indexBuffer,
			subsample_factor: def.subsample_factor,
			ob_lod_level,
			model_lod_level
		},
		bvh: bvhTransfer
		// @ts-expect-error - incorrect TS interface
	}, [
		def.groupsBuffer,	// Transferred Buffers
		def.positionBuffer,
		def.indexBuffer,
		bvhTransfer.bvhIndexBuffer,
		bvhTransfer.aabbBuffer,
		bvhTransfer.dataBuffer,
	]);
	// Don't transfer...
	// bvhTransfer.triIndexBuffer, // Copy of def.indexBuffer
	// bvhTransfer.triPositionBuffer // Copy of def.positionBuffer

}

// Handle a task message from the main thread
self.onmessage = (request: MessageEvent<MeshLoaderRequest>) => {
	const task = request.data;

	task.bmesh != null
		? processBMeshRequest(task.bmesh, task.pos, task.sizeFactor)
		: processVoxelMeshRequest(task.voxels, task.pos, task.sizeFactor);
};
