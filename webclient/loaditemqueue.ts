/*=====================================================================
loaditemqueue.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as THREE from './build/three.module.js';
import { MeshLoaderRequest } from './loader/message.js';

// The loadItemQueue can store either a voxelMesh or an URL to a batched mesh definition.
export class LoadItemQueueItem {
	pos: THREE.Vector3;
	size_factor: number;
	request: MeshLoaderRequest;

	constructor(request: MeshLoaderRequest) {
		const pos = request.pos;
		this.pos = new THREE.Vector3(pos[0], pos[1], pos[2]);
		this.size_factor = request.sizeFactor;
		this.request = request;
	}
}

export class LoadItemQueue {
	public items: Array<LoadItemQueueItem>;

	public constructor() {
		this.items = [];
	}

	public enqueueItem(item: LoadItemQueueItem): void {

		//console.log("enqueueItem(): Added queue item: '" + item.URL + "' (New queue size: " + (this.items.length + 1) + ")");

		this.items.push(item);
	}

	public sortQueue(campos: THREE.Vector3) { // Sort queue (by item distance to camera)

		this.items.sort(function (a, b) {
			const a_priority = a.pos.distanceTo(campos) * a.size_factor; // Larger priority = load later
			const b_priority = b.pos.distanceTo(campos) * b.size_factor;
			return a_priority - b_priority;
		});

		//for (let i = 0; i < this.items.length; ++i) {
		//	console.log("item " + i + ", dist2: " + this.items[i].pos.distanceToSquared(campos));
		//}
	}

	public length(): number { return this.items.length; }

	dequeueItem(): MeshLoaderRequest {
		//console.log("Dequeueing item '" + this.items[0].URL + "' from download queue.  (New queue size: " + (this.items.length - 1) + ")");
		console.assert(this.items.length > 0);
		return this.items.shift().request; // Removes first item from list and returns it.
	}
}
