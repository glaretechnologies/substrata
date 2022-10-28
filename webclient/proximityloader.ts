/*=====================================================================
proximityloader.ts
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as THREE from './build/three.module.js';


/*
Based on ProximityLoader c++ class.

When the camera moves close to a new grid cell, calls the newCellInProximity callback.
This allows sending of a QueryObjects message to the server.
*/
export class ProximityLoader {
	load_distance: number;

	last_cam_pos: THREE.Vector3 = new THREE.Vector3(0, 0, 0);

	callback_function: (cell_x: number, cell_y: number, cell_z: number) => void;


	constructor(load_distance: number, callback_function) {
		this.load_distance = load_distance;
		this.callback_function = callback_function;
	}

	// Notify the ProximityLoader that the camera has moved
	updateCamPos(new_cam_pos: THREE.Vector3) {
		if (new_cam_pos.distanceTo(this.last_cam_pos) > 1.0) {

			const CELL_WIDTH = 200.0; // NOTE: has to be the same value as in WorkerThread.cpp
			const recip_cell_w = 1 / CELL_WIDTH;

			const old_begin_x = Math.floor((this.last_cam_pos.x - this.load_distance) * recip_cell_w);
			const old_begin_y = Math.floor((this.last_cam_pos.y - this.load_distance) * recip_cell_w);
			const old_begin_z = Math.floor((this.last_cam_pos.z - this.load_distance) * recip_cell_w);
			const old_end_x   = Math.floor((this.last_cam_pos.x + this.load_distance) * recip_cell_w);
			const old_end_y   = Math.floor((this.last_cam_pos.y + this.load_distance) * recip_cell_w);
			const old_end_z   = Math.floor((this.last_cam_pos.z + this.load_distance) * recip_cell_w);
			

			// Iterate over grid cells around new_cam_pos, call the callback function for any cells that were not in load distance before.
			{
				const begin_x = Math.floor((new_cam_pos.x - this.load_distance) * recip_cell_w);
				const begin_y = Math.floor((new_cam_pos.y - this.load_distance) * recip_cell_w);
				const begin_z = Math.floor((new_cam_pos.z - this.load_distance) * recip_cell_w);
				const end_x   = Math.floor((new_cam_pos.x + this.load_distance) * recip_cell_w);
				const end_y   = Math.floor((new_cam_pos.y + this.load_distance) * recip_cell_w);
				const end_z   = Math.floor((new_cam_pos.z + this.load_distance) * recip_cell_w);

				for (let z = begin_z; z <= end_z; ++z)
				for (let y = begin_y; y <= end_y; ++y)
				for (let x = begin_x; x <= end_x; ++x)
				{
					const is_in_old_cells =
						x >= old_begin_x && y >= old_begin_y && z >= old_begin_z &&
						x <= old_end_x   && y <= old_end_y   && z <= old_end_z;

					if (!is_in_old_cells) {
						// console.log("ProximityLoader: Loading cell " + x + ", " + y + ", " + z);
						if (this.callback_function)
							this.callback_function(x, y, z);
					}
				}
			}

			this.last_cam_pos.copy(new_cam_pos);
		}
	}

	// Sets initial camera position
	// Returns query AABB
	setCameraPosForNewConnection(initial_cam_pos: THREE.Vector3): Array<number> {
		this.last_cam_pos.copy(initial_cam_pos);

		const CELL_WIDTH = 200.0; // NOTE: has to be the same value as in WorkerThread.cpp
		const recip_cell_w = 1 / CELL_WIDTH;

		// NOTE: Important to use the same maths here for determining which cells to load as we use in updateCamPos() above.
		// Otherwise some objects will not be loaded in some circumstances.
		const begin_x	= Math.floor((initial_cam_pos.x - this.load_distance) * recip_cell_w);
		const begin_y	= Math.floor((initial_cam_pos.y - this.load_distance) * recip_cell_w);
		const begin_z	= Math.floor((initial_cam_pos.z - this.load_distance) * recip_cell_w);
		const end_x		= Math.floor((initial_cam_pos.x + this.load_distance) * recip_cell_w); // inclusive upper bound
		const end_y		= Math.floor((initial_cam_pos.y + this.load_distance) * recip_cell_w);
		const end_z		= Math.floor((initial_cam_pos.z + this.load_distance) * recip_cell_w);

		return [begin_x * CELL_WIDTH, begin_y * CELL_WIDTH, begin_z * CELL_WIDTH, (end_x + 1) * CELL_WIDTH, (end_y + 1) * CELL_WIDTH, (end_z + 1) * CELL_WIDTH];
	}
}
