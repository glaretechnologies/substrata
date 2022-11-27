/*=====================================================================
voxelloading.ts
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as fzstd from './fzstd.js'; // from 'https://cdn.skypack.dev/fzstd?min';
import BVH, { Triangles } from './physics/bvh.js';

function decompressVoxels(compressed_voxels: ArrayBuffer): Int32Array {
	let decompressed_voxels_uint8 = fzstd.decompress(new Uint8Array(compressed_voxels));

	// A voxel is
	// Vec3<int> pos;
	// int mat_index; // Index into materials

	let voxel_data = new Int32Array(decompressed_voxels_uint8.buffer, decompressed_voxels_uint8.byteOffset, decompressed_voxels_uint8.byteLength / 4);


	// Do a pass over the data to get the total number of voxels, so that we can allocate voxels_out all at once.
	let total_num_voxels = 0;
	let read_i = 0;
	let num_mats = voxel_data[read_i++];
	if (num_mats > 2000)
		throw "Too many voxel materials";
	for (let m = 0; m < num_mats; ++m)
	{
		let count = voxel_data[read_i++]; // Number of voxels with this material.
		if (count < 0 || count > 64000000)
			throw "Voxel count is too large: " + count.toString();

		read_i += 3 * count; // Skip over voxel data.
		total_num_voxels += count;
	}

	let voxels_out: Int32Array = new Int32Array(total_num_voxels * 4); // Store 4 ints per voxel (see above)

	let cur_x: number = 0;
	let cur_y: number = 0;
	let cur_z: number = 0;

	// Reset stream read index to beginning.
	read_i = 0;
	num_mats = voxel_data[read_i++];

	let write_i = 0;
	for(let m=0; m<num_mats; ++m)
	{
		let count = voxel_data[read_i++];
		//console.log("mat: " + m + ", count: " + count)
		for(let i=0; i<count; ++i)
		{
			let rel_x = voxel_data[read_i++];
			let rel_y = voxel_data[read_i++];
			let rel_z = voxel_data[read_i++];

			//pos = current_pos + relative_pos;
			let v_x = cur_x + rel_x;
			let v_y = cur_y + rel_y;
			let v_z = cur_z + rel_z;

			voxels_out[write_i++] = v_x;
			voxels_out[write_i++] = v_y;
			voxels_out[write_i++] = v_z;
			voxels_out[write_i++] = m;

			//console.log("Added voxel at " + v_x + ", " + v_y + ", " + v_z + " with mat " + m);

			cur_x = v_x;
			cur_y = v_y;
			cur_z = v_z;
		}
	}

	return voxels_out;
}

export interface VoxelMeshData {
	groupsBuffer: ArrayBuffer
	positionBuffer: ArrayBuffer
	indexBuffer: ArrayBuffer
	subsample_factor: number
	bvh: BVH
}

// Does greedy meshing.  Adapted from VoxelMeshBuilding::doMakeIndigoMeshForVoxelGroupWith3dArray()
function doMakeMeshForVoxels(voxels: Int32Array, subsample_factor: number, mats_transparent_: Array<boolean>): VoxelMeshData
{
	let num_voxels = voxels.length / 4;

	//console.log("doMakeMeshForVoxels():");
	//console.log("num_voxels:" + num_voxels);

	// Get overall min and max coords
	let min_x = 1000000000;
	let min_y = 1000000000;
	let min_z = 1000000000;
	let max_x = -1000000000;
	let max_y = -1000000000;
	let max_z = -1000000000;
	let max_mat_index = 0;

	for(let i=0; i<num_voxels; ++i) {
		let v_x = Math.trunc(voxels[i*4 + 0] / subsample_factor);
		let v_y = Math.trunc(voxels[i*4 + 1] / subsample_factor);
		let v_z = Math.trunc(voxels[i*4 + 2] / subsample_factor);
		let mat_i = voxels[i*4 + 3];

		min_x = Math.min(min_x, v_x);
		min_y = Math.min(min_y, v_y);
		min_z = Math.min(min_z, v_z);
		max_x = Math.max(max_x, v_x);
		max_y = Math.max(max_y, v_y);
		max_z = Math.max(max_z, v_z);

		max_mat_index = Math.max(max_mat_index, mat_i)
	}

	let bounds_min = new Int32Array(3);
	bounds_min[0] = min_x;
	bounds_min[1] = min_y;
	bounds_min[2] = min_z;

	let bounds_max = new Int32Array(3);
	bounds_max[0] = max_x;
	bounds_max[1] = max_y;
	bounds_max[2] = max_z;


	let span_x = max_x - min_x + 1;
	let span_y = max_y - min_y + 1;
	let span_z = max_z - min_z + 1;

	let res = new Int32Array(3);
	res[0] = span_x;
	res[1] = span_y;
	res[2] = span_z;

	// console.log("span_x:")
	// console.log(span_x)
	// console.log("span_y:")
	// console.log(span_y)
	// console.log("span_z:")
	// console.log(span_z)


	// Build a local array of mat-transparent booleans, one for each material.  If no such entry in mats_transparent_ for a given index, assume opaque.
	let mat_transparent = new Int32Array(256);
	for (let i = 0; i < 256; ++i)
		mat_transparent[i] = ((i < mats_transparent_.length) && mats_transparent_[i]) ? 1 : 0;

	// Make a 3d-array, which will hold 1 material index per voxel.
	let voxel_grid = new Uint8Array(span_x * span_y * span_z);
	for(let i=0; i<span_x * span_y * span_z; ++i)
		voxel_grid[i] = 255;
	
	// Splat voxels into the grid
	for(let i=0; i<num_voxels; ++i) {
		let v_x = Math.trunc(voxels[i*4 + 0] / subsample_factor);
		let v_y = Math.trunc(voxels[i*4 + 1] / subsample_factor);
		let v_z = Math.trunc(voxels[i*4 + 2] / subsample_factor);
		let mat_i = voxels[i*4 + 3];

		voxel_grid[(v_z - min_z) * (span_x * span_y) + (v_y - min_y) * span_x + (v_x - min_x)] = mat_i;
	}

	// let geometry = new THREE.BufferGeometry();
	let vert_coords = []

	// For each material, we will have a list of vertex indices defining triangles with the given material
	let mat_vert_indices = [];

	let num_mats = max_mat_index + 1;
	for (let i = 0; i < num_mats; ++i) {
		mat_vert_indices.push([]); // Create an empty array of vertex indices for the material
	}

	let no_voxel_mat = 255;

	// For each dimension (x, y, z)
	for(let dim=0; dim<3; ++dim)
	{
		// Want the a_axis x b_axis = dim_axis
		let dim_a = 0;
		let dim_b = 0;
		if(dim == 0) {
			dim_a = 1;
			dim_b = 2;
		}
		else if(dim == 1) {
			dim_a = 2;
			dim_b = 0;
		}
		else { // dim == 2:
			dim_a = 0;
			dim_b = 1;
		}

		// Get the extents along dim_a, dim_b
		let a_min = bounds_min[dim_a];
		let a_size = res[dim_a];
		
		let b_min = bounds_min[dim_b];
		let b_size = res[dim_b];

		// Walk from lower to greater coords, look for downwards facing faces
		let dim_min = bounds_min[dim];
		let dim_size = res[dim];

		//console.log("a_min: " + a_min);
		//console.log("a_size: " + a_size);
		//console.log("b_min: " + b_min);
		//console.log("b_size: " + b_size);
		//console.log("dim_min: " + dim_min);
		//console.log("dim_size: " + dim_size);

		// An array of faces that still need to be processed.  We store the face material index if the face needs to be processed, and no_voxel_mat otherwise.  Processed = included in a greedy quad already.
		let face_needed_x_span = a_size;
		let face_needed_mat = new Uint8Array(a_size * b_size);

		for(let dim_coord = 0; dim_coord < dim_size; ++dim_coord)
		{
			let vox_indices = new Int32Array(3);
			vox_indices[dim] = dim_coord;
			let adjacent_vox_indices = new Int32Array(3);
			adjacent_vox_indices[dim] = dim_coord - 1;

			// Build face_needed data for this slice
			for(let y = 0; y < b_size; ++y)
			for(let x = 0; x < a_size; ++x)
			{
				vox_indices[dim_a] = x;
				vox_indices[dim_b] = y;

				let this_face_needed_mat = no_voxel_mat;
				let vox_mat_index = voxel_grid[vox_indices[2] * (span_x * span_y) + vox_indices[1] * span_x + vox_indices[0]];
				if(vox_mat_index != no_voxel_mat) // If there is a voxel here
				{
					adjacent_vox_indices[dim_a] = x;
					adjacent_vox_indices[dim_b] = y;

					if (dim_coord > 0) // If adjacent vox indices are in array bounds: (if dim_coord - 1 >= 0)
					{
						let adjacent_vox_mat_index = voxel_grid[adjacent_vox_indices[2] * (span_x * span_y) + adjacent_vox_indices[1] * span_x + adjacent_vox_indices[0]];

						if ((adjacent_vox_mat_index == no_voxel_mat) || // If adjacent voxel is empty, or
							((mat_transparent[adjacent_vox_mat_index] != 0) && (adjacent_vox_mat_index != vox_mat_index))) // the adjacent voxel is transparent, and the adjacent voxel has a different material.
							this_face_needed_mat = vox_mat_index;
					}
					else {
						this_face_needed_mat = vox_mat_index;
					}
				}
				face_needed_mat[y * face_needed_x_span + x] = this_face_needed_mat;
			}

			// For each voxel face:
			for (let start_y = 0; start_y < b_size; ++start_y)
			for (let start_x = 0; start_x < a_size; ++start_x) {

				let start_face_needed_mat = face_needed_mat[start_y * face_needed_x_span + start_x];
				if (start_face_needed_mat != no_voxel_mat) { // If we need a face here:
					
					// Start a quad here (start corner at (start_x, start_y))
					// The quad will range from (start_x, start_y) to (end_x, end_y)
					let end_x = start_x + 1;
					let end_y = start_y + 1;

					let x_increase_ok = true;
					let y_increase_ok = true;
					while(x_increase_ok || y_increase_ok)
					{
						// Try and increase in x direction
						if(x_increase_ok)
						{
							if (end_x < a_size) // If there is still room to increase in x direction:
							{
								// Check y values for new x = end_x
								for(let y = start_y; y < end_y; ++y)
									if (face_needed_mat[y * face_needed_x_span + end_x] != start_face_needed_mat)
									{
										x_increase_ok = false;
										break;
									}

								if(x_increase_ok)
									end_x++;
							}
							else
								x_increase_ok = false;
						}

						// Try and increase in y direction
						if(y_increase_ok)
						{
							if (end_y < b_size)
							{
								// Check x values for new y = end_y
								for(let x = start_x; x < end_x; ++x)
									if (face_needed_mat[end_y * face_needed_x_span + x] != start_face_needed_mat)
									{
										y_increase_ok = false;
										break;
									}

								if(y_increase_ok)
									end_y++;
							}
							else
								y_increase_ok = false;
						}
					}

					// We have worked out the greedy quad.  Mark elements in it as processed
					for(let y=start_y; y < end_y; ++y)
					for(let x=start_x; x < end_x; ++x)
						face_needed_mat[y * face_needed_x_span + x] = no_voxel_mat;

					
					// Add the greedy quad
					let start_x_coord = start_x + a_min;
					let start_y_coord = start_y + b_min;
					let end_x_coord = end_x + a_min;
					let end_y_coord = end_y + b_min;

					let v_i = new Int32Array(4); // quad vert indices
					let v = new Float32Array(3); // Vertex position coordinates
					v[dim] = dim_coord + dim_min;
					{
						// bot left
						v[dim_a] = start_x_coord;
						v[dim_b] = start_y_coord;

						v_i[0] = vert_coords.length / 3;

						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{
						// top left
						v[dim_a] = start_x_coord;
						v[dim_b] = end_y_coord;

						v_i[1] = vert_coords.length / 3;

						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{
						// top right
						v[dim_a] = end_x_coord;
						v[dim_b] = end_y_coord;

						v_i[2] = vert_coords.length / 3;

						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{
						// bot right
						v[dim_a] = end_x_coord;
						v[dim_b] = start_y_coord;

						v_i[3] = vert_coords.length / 3;

						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					
					// Append vertex indices to the list of vertex indices for the current material
					mat_vert_indices[start_face_needed_mat].push(v_i[0]);
					mat_vert_indices[start_face_needed_mat].push(v_i[1]);
					mat_vert_indices[start_face_needed_mat].push(v_i[2]);

					mat_vert_indices[start_face_needed_mat].push(v_i[0]);
					mat_vert_indices[start_face_needed_mat].push(v_i[2]);
					mat_vert_indices[start_face_needed_mat].push(v_i[3]);
				}
			}

			//================= Do upper faces along dim ==========================
			// Build face_needed data for this slice

			vox_indices[dim] = dim_coord;
			adjacent_vox_indices[dim] = dim_coord + 1;
			for (let y = 0; y < b_size; ++y)
			for (let x = 0; x < a_size; ++x)
			{
				vox_indices[dim_a] = x;
				vox_indices[dim_b] = y;
			
				let this_face_needed_mat = no_voxel_mat;
				let vox_mat_index = voxel_grid[vox_indices[2] * (span_x * span_y) + vox_indices[1] * span_x + vox_indices[0]];
				if (vox_mat_index != no_voxel_mat) // If there is a voxel here with mat_i
				{
					adjacent_vox_indices[dim_a] = x;
					adjacent_vox_indices[dim_b] = y;

					if (dim_coord < dim_size - 1) // If adjacent vox indices are in array bounds: (if dim_coord + 1 < dim_size)
					{
						let adjacent_vox_mat_index = voxel_grid[adjacent_vox_indices[2] * (span_x * span_y) + adjacent_vox_indices[1] * span_x + adjacent_vox_indices[0]];

						if ((adjacent_vox_mat_index == no_voxel_mat) ||
							((mat_transparent[adjacent_vox_mat_index] != 0) && (adjacent_vox_mat_index != vox_mat_index)))
							this_face_needed_mat = vox_mat_index;
					}
					else {
						this_face_needed_mat = vox_mat_index;
					}
				}
				face_needed_mat[y * face_needed_x_span + x] = this_face_needed_mat;
			}
			
			// For each voxel face:
			for (let start_y = 0; start_y < b_size; ++start_y)
			for (let start_x = 0; start_x < a_size; ++start_x) {
			
				let start_face_needed_mat = face_needed_mat[start_y * face_needed_x_span + start_x];
				if (start_face_needed_mat != no_voxel_mat) { // If we need a face here:
			
					// Start a quad here (start corner at (start_x, start_y))
					// The quad will range from (start_x, start_y) to (end_x, end_y)
					let end_x = start_x + 1;
					let end_y = start_y + 1;

					let x_increase_ok = true;
					let y_increase_ok = true;
					while (x_increase_ok || y_increase_ok) {
						// Try and increase in x direction
						if (x_increase_ok) {
							if (end_x < a_size) // If there is still room to increase in x direction:
							{
								// Check y values for new x = end_x
								for (let y = start_y; y < end_y; ++y)
									if (face_needed_mat[y * face_needed_x_span + end_x] != start_face_needed_mat) {
										x_increase_ok = false;
										break;
									}

								if (x_increase_ok)
									end_x++;
							}
							else
								x_increase_ok = false;
						}

						// Try and increase in y direction
						if (y_increase_ok) {
							if (end_y < b_size) {
								// Check x values for new y = end_y
								for (let x = start_x; x < end_x; ++x)
									if (face_needed_mat[end_y * face_needed_x_span + x] != start_face_needed_mat) {
										y_increase_ok = false;
										break;
									}

								if (y_increase_ok)
									end_y++;
							}
							else
								y_increase_ok = false;
						}
					}

					// We have worked out the greedy quad.  Mark elements in it as processed
					for (let y = start_y; y < end_y; ++y)
					for (let x = start_x; x < end_x; ++x)
						face_needed_mat[y * face_needed_x_span + x] = no_voxel_mat;

					// Add the greedy quad

					let quad_dim_coord = dim_coord + dim_min + 1.0;
					let start_x_coord = start_x + a_min;
					let start_y_coord = start_y + b_min;
					let end_x_coord = end_x + a_min;
					let end_y_coord = end_y + b_min;
				
					let v_i = new Int32Array(4); // quad vert indices
					let v = new Float32Array(3);
					v[dim] = quad_dim_coord;
					{ // Add bot left vert
						v[dim_a] = start_x_coord;
						v[dim_b] = start_y_coord;
								
						v_i[0] = vert_coords.length / 3;
			
						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{ // bot right
						v[dim_a] = end_x_coord;
						v[dim_b] = start_y_coord;
			
						v_i[1] = vert_coords.length / 3;
			
						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{ // top right
						v[dim_a] = end_x_coord;
						v[dim_b] = end_y_coord;
			
						v_i[2] = vert_coords.length / 3;
			
						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}
					{ // top left
						v[dim_a] = start_x_coord;
						v[dim_b] = end_y_coord;
			
						v_i[3] = vert_coords.length / 3;
			
						vert_coords.push(v[0]);
						vert_coords.push(v[1]);
						vert_coords.push(v[2]);
					}

					// Append vertex indices to the list of vertex indices for the current material
					mat_vert_indices[start_face_needed_mat].push(v_i[0]);
					mat_vert_indices[start_face_needed_mat].push(v_i[1]);
					mat_vert_indices[start_face_needed_mat].push(v_i[2]);

					mat_vert_indices[start_face_needed_mat].push(v_i[0]);
					mat_vert_indices[start_face_needed_mat].push(v_i[2]);
					mat_vert_indices[start_face_needed_mat].push(v_i[3]);
				}
			}
		}
	} // End For each dim



	let combined_indices_size = 0;
	for (let i = 0; i < num_mats; ++i) {
		combined_indices_size += mat_vert_indices[i].length;
	}

	let combined_vert_indices = new Uint32Array(combined_indices_size);
	const groups = new Array<number>()

	let cur_offset = 0;
	for (let i = 0; i < num_mats; ++i) {
		let mat_vert_indices_i = mat_vert_indices[i];
		if (mat_vert_indices_i.length > 0) {
			// geometry.addGroup(/*start index=*/cur_offset, /*count=*/mat_vert_indices_i.length, /*mat index=*/i);
			groups.push(/*start index=*/cur_offset, /*count=*/mat_vert_indices_i.length, /*mat index=*/i);

			for (let z = 0; z < mat_vert_indices_i.length; ++z) {
				combined_vert_indices[cur_offset++] = mat_vert_indices_i[z];
			}
		}
	}

	const vert_float32_array = new Float32Array(vert_coords);

	const triangles = new Triangles(
		vert_float32_array, // vertices
		combined_vert_indices, // index
		3 // stride: vertices are tightly packed
	);

	return {
		groupsBuffer: new Uint32Array(groups).buffer,
		indexBuffer: combined_vert_indices.buffer,
		positionBuffer: vert_float32_array.buffer,
		bvh: new BVH(triangles),
		subsample_factor
	};
}


export function makeMeshForVoxelGroup(compressed_voxels: ArrayBuffer, model_lod_level: number, mats_transparent: Array<boolean>): VoxelMeshData {

	let voxels: Int32Array = decompressVoxels(compressed_voxels);
	// voxels is an Int32Array array of voxel data, with each voxel laid out as (pos_x, pos_y, pos_z, mat_index)

	let num_voxels = voxels.length / 4;

	// Work out subsample_factor to use
	let max_model_lod_level = (num_voxels > 256) ? 2 : 0;
	let use_model_lod_level = Math.min(model_lod_level, max_model_lod_level);

	let subsample_factor = 1;
	if (use_model_lod_level == 1)
		subsample_factor = 2;
	else if (use_model_lod_level == 2)
		subsample_factor = 4;

	//console.log("makeMeshForVoxelGroup");
	//console.log("num_voxels: " + num_voxels);

	return doMakeMeshForVoxels(voxels, subsample_factor, mats_transparent);
}
