/*=====================================================================
MeshLoaderWorker.ts
---------------
Copyright Glare Technologies Limited 2022 -

NOTE: The Worker cannot import any code, so ALL building code has to
be included below.  This first version is for Chrome only so it's
easier to debug before we harden it for all browsers.
=====================================================================*/

import { MeshLoaderRequest, Voxels } from './message.js';
import { loadBatchedMesh } from '../bmeshloading.js';
import { makeMeshForVoxelGroup } from '../voxelloading.js';
import { getIndexType } from '../physics/bvh.js';

function processBMeshRequest (url: string): void {
	const encoded_url = encodeURIComponent(url);
	fetch('/resource/' + encoded_url).then(resp => {
		if(resp.status >= 200 && resp.status < 300) {
			return resp.arrayBuffer();
		} else {
			throw new Error('Request for bmesh \'' + url + '\' encountered an error: ' + resp.status);
		}
	}).then((buffer: ArrayBuffer) => {
		const {
			groupsBuffer, indexType, indexBuffer, interleaved, interleavedStride, attributes, bvh
		} = loadBatchedMesh(buffer);

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
	});
}

function processVoxelMeshRequest (voxels: Voxels): void {
	const { uid, model_lod_level, ob_lod_level } = voxels;

	const def = makeMeshForVoxelGroup(voxels.compressedVoxels, voxels.model_lod_level, voxels.mats_transparent);

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
		// bvhTransfer.triIndexBuffer, // Copy of def.indexBuffer
		// bvhTransfer.triPositionBuffer
	]);
}

self.onmessage = (request: MessageEvent<MeshLoaderRequest>) => {
	const task = request.data;
	task.bmesh != null
		? processBMeshRequest(task.bmesh)
		: processVoxelMeshRequest(task.voxels);
};
