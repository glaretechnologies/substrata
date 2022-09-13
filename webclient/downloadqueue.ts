/*=====================================================================
downloadqueue.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as THREE from './build/three.module.js';


export function sizeFactorForAABBWSLongestLen(aabb_ws_longest_len) {
	// object projected angle    theta ~= aabb_ws.longestLength() / ob_dist

	// We will sort in ascending order by 1 / theta so that objects with a larger projected angle are loaded first.

	// 1 / theta = 1 / (aabb_ws.longestLength() / ob_dist) = ob_dist / aabb_ws.longestLength()

	const min_len = 0.5; // Objects smaller than 0.5m are considered just as important as 0.5m wide objects.

	return 1.0 / Math.max(min_len, aabb_ws_longest_len);
}


//export function sizeFactorForAABBWS(aabb_ws)
//{
//	// object projected angle    theta ~= aabb_ws.longestLength() / ob_dist

//	// We will sort in ascending order by 1 / theta so that objects with a larger projected angle are loaded first.

//	// 1 / theta = 1 / (aabb_ws.longestLength() / ob_dist) = ob_dist / aabb_ws.longestLength()

//	const min_len = 0.5; // Objects smaller than 0.5m are considered just as important as 0.5m wide objects.

//	return 1.0 / Math.max(min_len, aabb_ws.longestLength());
//}


export class DownloadQueueItem
{
	pos: THREE.Vector3;
	size_factor: number;
	URL: string;
	is_texture: boolean;

	constructor(pos_: THREE.Vector3, size_factor_: number, URL_: string, is_texture_: boolean) {
		this.pos = pos_;
		this.size_factor = size_factor_;
		this.URL = URL_;
		this.is_texture = is_texture_;
	}

	

	//Vec4f pos;
	//float size_factor;
	//std::string URL;
};


export class DownloadQueue {

	items: Array<DownloadQueueItem>;

	constructor() {
		this.items = [];
	}

	enqueueItem(item) {

		//console.log("enqueueItem(): Added queue item: '" + item.URL + "' (New queue size: " + (this.items.length + 1) + ")");

		this.items.push(item);
	}

	sortQueue(campos) { // Sort queue (by item distance to camera)

		this.items.sort(function (a, b) {
			const a_priority = a.pos.distanceTo(campos) * a.size_factor; // Larger priority = load later
			const b_priority = b.pos.distanceTo(campos) * b.size_factor;
			return a_priority - b_priority;
		});

		//for (let i = 0; i < this.items.length; ++i) {
		//	console.log("item " + i + ", dist2: " + this.items[i].pos.distanceToSquared(campos));
		//}
	}

	dequeueItem() {
		//console.log("Dequeueing item '" + this.items[0].URL + "' from download queue.  (New queue size: " + (this.items.length - 1) + ")");
		return this.items.shift(); // Removes first item from list and returns it.
	}
}
