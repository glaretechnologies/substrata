/*=====================================================================
MeshLoader.ts
---------------
Copyright Glare Technologies Limited 2022 -

Handles fetching, loading, and construction of the BMesh and Voxel mesh
types in a worker.  Transfers out the buffers from the worker to the
caller for uploading to the WebGL context.
=====================================================================*/

import { LoaderError, MeshLoaderRequest, MeshLoaderResponse } from './message.js';
import { clamp } from '../maths/functions.js';
import { LoadItemQueue } from '../loaditemqueue.js';

export type MeshLoaderCallback = (meshLoaderResponse: MeshLoaderResponse) => void
export type MeshLoaderErrorCallback = (error: ErrorEvent | LoaderError) => void

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
	private readonly queue_: LoadItemQueue;
	private readonly jobCounter_: Int16Array;
	private readonly maxInputLen_: number;

	private onmessage_: MeshLoaderCallback;
	private onerror_: MeshLoaderErrorCallback;

	// The maxInputLen restricts the size of the per-worker queues
	public constructor (queue: LoadItemQueue, poolSize: number, maxInputLen=2) {
		this.queue_ = queue;
		this.maxInputLen_ = clamp(maxInputLen, 1, 255);
		poolSize = clamp(poolSize, 1, 10);
		this.worker_ = new Array<Worker>();
		this.jobCounter_ = new Int16Array(poolSize);

		for(let i = 0; i !== poolSize; ++i) {
			// NOTE: For now, we are using module types in order to not concatenate all the code into the worker just yet.
			this.worker_[i] = new Worker('/webclient/loader/MeshLoaderWorker.js', { type: 'module' });
			this.worker_[i].onmessage = (ev: MessageEvent<MeshLoaderResponse>) => this.handleResponse(ev.data, i);
			this.worker_[i].onerror = (ev: ErrorEvent | LoaderError) => this.handleError(ev, i);
		}

		this.onerror_ = (ev: ErrorEvent | LoaderError) => { console.error('MeshLoader error:', ev); };
	}

	private handleResponse (response: MeshLoaderResponse, workerId: number): void {
		this.jobCounter_[workerId] -= 1;
		console.assert(this.jobCounter_[workerId] >= 0);

		if(response.error) {
			this.onerror_(response.error);
		} else {
			this.onmessage_(response);
		}
	}

	private handleError (ev: ErrorEvent | LoaderError, workerId: number): void {
		this.jobCounter_[workerId] -= 1;
		console.assert(this.jobCounter_[workerId] >= 0);

		this.onerror_(ev);
	}

	public get poolSize (): number { return this.worker_.length; }

	public set onmessage (cb: MeshLoaderCallback) { this.onmessage_ = cb;	}
	public set onerror (cb: MeshLoaderErrorCallback) { this.onerror_ = cb; }

	// We store the position in the message so that we can sort both before and after the mesh was loaded.
	public processRequests() {
		const q = this.queue_;
		while(q.items.length > 0) {
			const workerId = findMinIndex(this.jobCounter_);
			if(this.jobCounter_[workerId] >= this.maxInputLen_) break;

			const req = this.queue_.dequeueItem();
			this.jobCounter_[workerId] += 1;
			this.worker_[workerId].postMessage(req);
		}
	}
}