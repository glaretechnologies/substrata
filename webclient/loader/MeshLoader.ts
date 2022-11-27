/*=====================================================================
MeshLoader.ts
---------------
Copyright Glare Technologies Limited 2022 -

Handles fetching, loading, and construction of the BMesh and Voxel mesh
types in a worker.  Transfers out the buffers from the worker to the
caller for uploading to the WebGL context.
=====================================================================*/

import { MeshLoaderRequest, MeshLoaderResponse, Voxels } from './message.js';
import { clamp } from '../maths/functions.js';

export type MeshLoaderCallback = (meshLoaderResponse: MeshLoaderResponse) => void
export type MeshLoaderErrorCallback = (error: ErrorEvent) => void

function findMinIndex(arr: ArrayLike<number>): number {
	let m = Number.MAX_VALUE;
	let idx = 0;
	for(let i = 0; i !== arr.length; ++i) {
		if(arr[i] < m) {
			idx = i;
			m = arr[i];
		}
	}
	return idx;
}

export default class MeshLoader {
	private readonly worker_: Worker[];
	private readonly jobQueue_: MeshLoaderRequest[];
	private readonly jobCounter_: Int16Array;
	private readonly maxInputLen_: number;

	private onmessage_: MeshLoaderCallback;
	private onerror_: MeshLoaderErrorCallback;

	public constructor (poolSize: number, maxInputLen=4) {
		this.jobQueue_ = new Array<MeshLoaderRequest>();
		this.maxInputLen_ = clamp(maxInputLen, 1, 255);
		poolSize = clamp(poolSize, 1, 10);
		this.worker_ = new Array<Worker>();
		this.jobCounter_ = new Int16Array(poolSize);

		for(let i = 0; i !== poolSize; ++i) {
			// NOTE: For now, we are using module types in order to not concatenate all the code into the worker just yet.
			this.worker_[i] = new Worker('/webclient/loader/MeshLoaderWorker.js', { type: 'module' });
			this.worker_[i].onmessage = (ev: MessageEvent<MeshLoaderResponse>) => this.handleResponse(ev.data, i);
			this.worker_[i].onerror = (ev: ErrorEvent) => this.handleError(ev.error, i);
		}

		this.onerror_ = (ev: ErrorEvent) => { console.error('MeshLoader error:', ev); };
	}

	private handleResponse (response: MeshLoaderResponse, workerId: number): void {
		this.jobCounter_[workerId] -= 1;
		console.assert(this.jobCounter_[workerId] >= 0);

		this.processQueue();
		this.onmessage_(response);
	}

	private handleError (ev: ErrorEvent, workerId: number): void {
		this.jobCounter_[workerId] -= 1;
		console.assert(this.jobCounter_[workerId] >= 0);

		this.processQueue();
		this.onerror_(ev);
	}

	public get poolSize (): number { return this.worker_.length; }

	public set onmessage (cb: MeshLoaderCallback) { this.onmessage_ = cb;	}
	public set onerror (cb: MeshLoaderErrorCallback) { this.onerror_ = cb; }

	public enqueueRequest (urlOrVoxels: string | Voxels, uid?: bigint): void {
		const req = {
			uid,
			...(
				typeof(urlOrVoxels) === 'string'
					? { bmesh: urlOrVoxels as string }
					: { voxels: urlOrVoxels as Voxels }
			)
		};

		this.jobQueue_.push(req);
		this.processQueue();
	}

	private processQueue (): void {
		while (this.jobQueue_.length > 0) {
			const workerId = findMinIndex(this.jobCounter_);
			if(this.jobCounter_[workerId] >= this.maxInputLen_) break;

			const req = this.jobQueue_.shift();

			this.jobCounter_[workerId] += 1;
			this.worker_[workerId].postMessage(req, req.voxels ? [req.voxels.compressedVoxels] : []);
		}
	}
}