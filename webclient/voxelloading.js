/*=====================================================================
voxelloading.js
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/

import * as fzstd from './fzstd.js'; // from 'https://cdn.skypack.dev/fzstd?min';
import * as THREE from './build/three.module.js';


function decompressVoxels(compressed_voxels) {
	let decompressed_voxels_uint8 = fzstd.decompress(new Uint8Array(compressed_voxels));
    //console.log("decompressed_voxels_uint8:");
    //console.log(decompressed_voxels_uint8);

    // A voxel is
    //Vec3<int> pos;
	//int mat_index; // Index into materials
    //let voxel_ints = new Uint32Array(decompressed_voxels))

	//console.log("decompressed_voxels_uint8.buffer:")
	//console.log(decompressed_voxels_uint8.buffer)

    let voxel_data = new Int32Array(decompressed_voxels_uint8.buffer, decompressed_voxels_uint8.byteOffset, decompressed_voxels_uint8.byteLength / 4);

	//console.log("voxel_data:");
   // console.log(voxel_data);

    let voxels_out = []

    

    let cur_x = 0;
    let cur_y = 0;
    let cur_z = 0;

    let read_i = 0;
	let num_mats = voxel_data[read_i++];
	//console.log("num_mats: " + num_mats)
    if(num_mats > 2000)
        throw "Too many voxel materials";
	        
    for(let m=0; m<num_mats; ++m)
	{
		let count = voxel_data[read_i++];
		//console.log("mat: " + m + ", count: " + count)
		for(let i=0; i<count; ++i)
		{
			//Vec3<int> relative_pos;
			// instream.readData(&relative_pos, sizeof(Vec3<int>));
            let rel_x = voxel_data[read_i++];
            let rel_y = voxel_data[read_i++];
            let rel_z = voxel_data[read_i++];

			//const Vec3<int> pos = current_pos + relative_pos;
            let v_x = cur_x + rel_x;
            let v_y = cur_y + rel_y;
            let v_z = cur_z + rel_z;

			//group_out.voxels.push_back(Voxel(pos, m));
            voxels_out.push(v_x);
            voxels_out.push(v_y);
            voxels_out.push(v_z);
            voxels_out.push(m);

            //console.log("Added voxel at " + v_x + ", " + v_y + ", " + v_z + " with mat " + m);

			cur_x = v_x;
            cur_y = v_y;
            cur_z = v_z;
		}
	}

	return voxels_out;
}


// Does greedy meshing.  Adapted from VoxelMeshBuilding::doMakeIndigoMeshForVoxelGroup()
// returns a THREE.BufferGeometry() object
function doMakeMeshForVoxels(voxels, num_mats)
{
	let num_voxels = voxels.length / 4;

	//console.log("doMakeMeshForVoxels():");
    //console.log("num_voxels:" + num_voxels);

	// Iterate over voxels, get voxel bounds for each material
	let bounds = new Int32Array(6 * num_mats) // For each mat: (min_x, min_y, min_z, max_x, max_y, max_z)

	for(let i=0; i<num_mats; ++i) {
		bounds[i*6 + 0] = 1000000000;
		bounds[i*6 + 1] = 1000000000;
		bounds[i*6 + 2] = 1000000000;
		bounds[i*6 + 3] = -1000000000;
		bounds[i*6 + 4] = -1000000000;
		bounds[i*6 + 5] = -1000000000;
	}

	for(let i=0; i<num_voxels; ++i) {
		let v_x = voxels[i*4 + 0];
		let v_y = voxels[i*4 + 1];
		let v_z = voxels[i*4 + 2];
		let mat_i = voxels[i*4 + 3];

		bounds[mat_i * 6 + 0] = Math.min(bounds[mat_i * 6 + 0], v_x);
		bounds[mat_i * 6 + 1] = Math.min(bounds[mat_i * 6 + 1], v_y);
		bounds[mat_i * 6 + 2] = Math.min(bounds[mat_i * 6 + 2], v_z);

		bounds[mat_i * 6 + 3] = Math.max(bounds[mat_i * 6 + 3], v_x);
		bounds[mat_i * 6 + 4] = Math.max(bounds[mat_i * 6 + 4], v_y);
		bounds[mat_i * 6 + 5] = Math.max(bounds[mat_i * 6 + 5], v_z);
	}

	/*let largest_vol = 0;
	for(let i=0; i<num_mats; ++i) {
		if(bounds[i * 6] == 1000000000)
			continue; // No voxels for this mat.

		let min_x = bounds[i*6 + 0];
		let min_y = bounds[i*6 + 1];
		let min_z = bounds[i*6 + 2];
		let max_x = bounds[i*6 + 3];
		let max_y = bounds[i*6 + 4];
		let max_z = bounds[i*6 + 5];

		let span_x = max_x - min_x;
		let span_y = max_y - min_y;
		let span_z = max_z - min_z;

		let vol = span_x * span_y * span_z;
		largest_vol = Math.max(vol, largest_vol);
	}*/

	//console.log("bounds:")
	//console.log(bounds)

	// Get overall min and max coords
	let min_x = 1000000000;
	let min_y = 1000000000;
	let min_z = 1000000000;
	let max_x = -1000000000;
	let max_y = -1000000000;
	let max_z = -1000000000;

	for(let i=0; i<num_mats; ++i) {
		if(bounds[i * 6] == 1000000000)
			continue; // No voxels for this mat.

		min_x = Math.min(min_x,  bounds[i*6 + 0]);
		min_y = Math.min(min_y,  bounds[i*6 + 1]);
		min_z = Math.min(min_z,  bounds[i*6 + 2]);
		max_x = Math.max(max_x,  bounds[i*6 + 3]);
		max_y = Math.max(max_y,  bounds[i*6 + 4]);
		max_z = Math.max(max_z,  bounds[i*6 + 5]);
	}

	let span_x = max_x - min_x + 1;
	let span_y = max_y - min_y + 1;
	let span_z = max_z - min_z + 1;

	// console.log("span_x:")
	// console.log(span_x)
	// console.log("span_y:")
	// console.log(span_y)
	// console.log("span_z:")
	// console.log(span_z)

	// Make a 3d-array, which will hold 1 material index per voxel.
	// Make the array big enough so it can hold the largest per-material voxel bounds
	let voxel_grid = new Int16Array(span_x * span_y * span_z);
	for(let i=0; i<span_x * span_y * span_z; ++i)
		voxel_grid[i] = -1;

	// Splat voxels into the grid
	for(let i=0; i<num_voxels; ++i) {
		let v_x = voxels[i*4 + 0];
		let v_y = voxels[i*4 + 1];
		let v_z = voxels[i*4 + 2];
		let mat_i = voxels[i*4 + 3];

		voxel_grid[(v_z - min_z) * (span_x * span_y) + (v_y - min_y) * span_x + (v_x - min_x)] = mat_i;
	}

	//console.log("voxel_grid:");
	//console.log(voxel_grid);

	let geometry = new THREE.BufferGeometry();
	let verts = []
	let tri_vert_indices = []

	for(let mat_i=0; mat_i<num_mats; ++mat_i) { // For each mat
	
		//console.log("processing mat " + mat_i)

		let mat_vert_indices_start = tri_vert_indices.length;

		if(bounds[mat_i * 6] == 1000000000)
			continue; // No voxels for this mat.

		// For each dimension (x, y, z)
		for(let dim=0; dim<3; ++dim)
		{
			//console.log("processing dim " + dim)
			
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

			//console.log("dim_a: " + dim_a)
			//console.log("dim_b: " + dim_b)

			// Get the extents along dim_a, dim_b
			let a_min = bounds[mat_i * 6 + dim_a    ];
			let a_end = bounds[mat_i * 6 + dim_a + 3] + 1;

			let b_min = bounds[mat_i * 6 + dim_b    ];
			let b_end = bounds[mat_i * 6 + dim_b + 3] + 1;

			// Walk from lower to greater coords, look for downwards facing faces
			let dim_min = bounds[mat_i * 6 + dim    ];
			let dim_end = bounds[mat_i * 6 + dim + 3] + 1;

			//console.log("a_min: " + a_min);
			//console.log("a_end: " + a_end);
			//console.log("b_min: " + b_min);
			//console.log("b_end: " + b_end);
			//console.log("dim_min: " + dim_min);
			//console.log("dim_end: " + dim_end);

			// Make a map to indicate processed voxel faces.  Processed = included in a greedy quad already.
			//Array2D<bool> face_needed(a_end - a_min, b_end - b_min);
			let face_needed_x_span = (a_end - a_min);
			let face_needed = new Uint8Array(face_needed_x_span * (b_end - b_min));
			//console.log("*********************************");
			//console.log("face_needed: ");
			//console.log(face_needed);
			//face_needed[0] = 123;
			//console.log("face_needed: ");
			//console.log(face_needed);
			//console.log("face_needed[0]: ");
			//console.log(face_needed[0]);


			for(let dim_coord = dim_min; dim_coord < dim_end; ++dim_coord)
			{
				let vox = new Int32Array(3);
				vox[dim] = dim_coord;
				let adjacent_vox_pos = new Int32Array(3);
				adjacent_vox_pos[dim] = dim_coord - 1;

				// Build face_needed data for this slice
				for(let y=b_min; y<b_end; ++y)
				for(let x=a_min; x<a_end; ++x)
				{
					vox[dim_a] = x;
					vox[dim_b] = y;

					//console.log("vox: ")
					//console.log(vox)

					let this_face_needed = 0;
					let grid_val = voxel_grid[(vox[2] - min_z) * (span_x * span_y) + (vox[1] - min_y) * span_x + (vox[0] - min_x)];
					//console.log("grid_val: " + grid_val)
					if(grid_val == mat_i) // If there is a voxel here with mat_i
					{
						adjacent_vox_pos[dim_a] = x;
						adjacent_vox_pos[dim_b] = y;

						//console.log("dim_coord: " + dim_coord)
						//console.log("dim_min: " + dim_min)
						if(dim_coord == dim_min) // If adjacent coords are out of voxel grid:
							this_face_needed = 1;
						else {
							let adj_grid_val = voxel_grid[(adjacent_vox_pos[2] - min_z) * (span_x * span_y) + (adjacent_vox_pos[1] - min_y) * span_x + (adjacent_vox_pos[0] - min_x)];
							if(adj_grid_val != mat_i) // If there is no adjacent voxel, or the adjacent voxel has a different material:
								this_face_needed = 1;
						}
					}
					//face_needed.elemthis_face_neededx - a_min, y - b_min) = this_face_needed;
					//console.log("this_face_needed: ");
					//console.log(this_face_needed);
					face_needed[(y - b_min) * face_needed_x_span + (x - a_min)] = this_face_needed;
				}

				//console.log("face_needed: ")
				//console.log(face_needed)
				//console.log("face_needed[0]: ")
				//console.log(face_needed[0])

				// For each voxel face:
				for(let start_y=b_min; start_y<b_end; ++start_y)
				for(let start_x=a_min; start_x<a_end; ++start_x) {
				
					//if(face_needed.elem(start_x - a_min, start_y - b_min)) // If we need a face here:
					if(face_needed[(start_y - b_min) * face_needed_x_span + (start_x - a_min)]) {
					
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
								if(end_x < a_end) // If there is still room to increase in x direction:
								{
									// Check y values for new x = end_x
									for(let y = start_y; y < end_y; ++y)
										if(!face_needed[(y - b_min) * face_needed_x_span + (end_x - a_min)]/*face_needed.elem(end_x - a_min, y - b_min)*/)
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
								if(end_y < b_end)
								{
									// Check x values for new y = end_y
									for(let x = start_x; x < end_x; ++x)
										if(!face_needed[(end_y - b_min) * face_needed_x_span + (x - a_min)]/*face_needed.elem(x - a_min, end_y - b_min)*/)
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
							//face_needed.elem(x - a_min, y - b_min) = false;
							face_needed[(y - b_min) * face_needed_x_span + (x - a_min)] = 0;

						// Add the greedy quad
						//unsigned int v_i[4];
						let v_i = []
						let v = new Float32Array(3);
						v[dim] = dim_coord;
						{
							// bot left
							v[dim_a] = start_x;
							v[dim_b] = start_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{
							// top left
							v[dim_a] = start_x;
							v[dim_b] = end_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{
							// top right
							v[dim_a] = end_x;
							v[dim_b] = end_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{
							// bot right
							v[dim_a] = end_x;
							v[dim_b] = start_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}

						// Append triangle vert indices
						tri_vert_indices.push(v_i[0])
						tri_vert_indices.push(v_i[1])
						tri_vert_indices.push(v_i[2])

						tri_vert_indices.push(v_i[0])
						tri_vert_indices.push(v_i[2])
						tri_vert_indices.push(v_i[3])
					}
				}

				//================= Do upper faces along dim ==========================
				// Build face_needed data for this slice

				//console.log("------------Building upper faces-----------");

				vox[dim] = dim_coord;
				adjacent_vox_pos[dim] = dim_coord + 1;
				for(let y=b_min; y<b_end; ++y)
				for(let x=a_min; x<a_end; ++x)
				{
					vox[dim_a] = x;
					vox[dim_b] = y;

					let this_face_needed = 0;
					let grid_val = voxel_grid[(vox[2] - min_z) * (span_x * span_y) + (vox[1] - min_y) * span_x + (vox[0] - min_x)];
					if(grid_val == mat_i)
					{
						adjacent_vox_pos[dim_a] = x;
						adjacent_vox_pos[dim_b] = y;

						if(dim_coord == dim_end - 1) // If adjacent voxel coords will be out of grid:
							this_face_needed = 1;
						else {
							let adj_grid_val = voxel_grid[(adjacent_vox_pos[2] - min_z) * (span_x * span_y) + (adjacent_vox_pos[1] - min_y) * span_x + (adjacent_vox_pos[0] - min_x)];
							if(adj_grid_val != mat_i) // If there is no adjacent voxel, or the adjacent voxel has a different material:
								this_face_needed = 1;
						}
					}
					//face_needed.elem(x - a_min, y - b_min) = this_face_needed;
					face_needed[(y - b_min) * face_needed_x_span + (x - a_min)] = this_face_needed;
				}

				//console.log("face_needed[0]: ");
				//console.log(face_needed[0]);

				// For each voxel face:
				for(let start_y=b_min; start_y<b_end; ++start_y)
				for(let start_x=a_min; start_x<a_end; ++start_x) {

					if(face_needed[(start_y - b_min) * face_needed_x_span + (start_x - a_min)]) {

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
								if(end_x < a_end) // If there is still room to increase in x direction:
								{
									// Check y values for new x = end_x
									for(let y = start_y; y < end_y; ++y)
										if(!face_needed[(y - b_min) * face_needed_x_span + (end_x - a_min)]/*face_needed.elem(end_x - a_min, y - b_min)*/)
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
								if(end_y < b_end)
								{
									// Check x values for new y = end_y
									for(let x = start_x; x < end_x; ++x)
										if(!face_needed[(end_y - b_min) * face_needed_x_span + (x - a_min)]/*face_needed.elem(x - a_min, end_y - b_min)*/)
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
							face_needed[(y - b_min) * face_needed_x_span + (x - a_min)] = 0;
							//face_needed.elem(x - a_min, y - b_min) = false;

						let quad_dim_coord = dim_coord + 1.0;

						// Add the greedy quad
						//unsigned int v_i[4];
						let v_i = []
						let v = new Float32Array(3);
						v[dim] = quad_dim_coord;
						{ // Add bot left vert
							v[dim_a] = start_x;
							v[dim_b] = start_y;
								
							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{ // bot right
							v[dim_a] = end_x;
							v[dim_b] = start_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{ // top right
							v[dim_a] = end_x;
							v[dim_b] = end_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
						{ // top left
							v[dim_a] = start_x;
							v[dim_b] = end_y;

							// Append vertex to verts
							v_i.push(verts.length/3)

							verts.push(v[0]);
							verts.push(v[1]);
							verts.push(v[2]);
						}
							
						// Append triangle vert indices
						tri_vert_indices.push(v_i[0])
						tri_vert_indices.push(v_i[1])
						tri_vert_indices.push(v_i[2])

						tri_vert_indices.push(v_i[0])
						tri_vert_indices.push(v_i[2])
						tri_vert_indices.push(v_i[3])
					}
				}
			}
		}

		geometry.addGroup(/*start index=*/mat_vert_indices_start, /*count=*/tri_vert_indices.length - mat_vert_indices_start, mat_i);
	} // End For each mat

	
	geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(verts), 3));
	geometry.setIndex(tri_vert_indices);
	return geometry;
}



// compressed_voxels is an ArrayBuffer 
// subsample_factor is an integer >= 1
// returns [THREE.BufferGeometry(), subsample_factor]
export function makeMeshForVoxelGroup(compressed_voxels, model_lod_level) {

	let voxels = decompressVoxels(compressed_voxels);
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

	if(subsample_factor == 1)
	{
		let max_mat_index = 0;
		for(let v = 0; v < num_voxels; ++v)
		{
			let mat_i = voxels[v*4 + 3];
			max_mat_index = Math.max(max_mat_index, mat_i);
		}
		let num_mats = max_mat_index + 1;

		return [doMakeMeshForVoxels(voxels, num_mats), subsample_factor];
	}
	else
	{
		let max_mat_index = 0;
		for(let v = 0; v < num_voxels; ++v)
		{
			voxels[v * 4 + 0] = Math.floor(voxels[v * 4 + 0] / subsample_factor);
			voxels[v * 4 + 1] = Math.floor(voxels[v * 4 + 1] / subsample_factor);
			voxels[v * 4 + 2] = Math.floor(voxels[v * 4 + 2] / subsample_factor);
			let mat_i = voxels[v*4 + 3];

			max_mat_index = Math.max(max_mat_index, mat_i);
		}
		let num_mats = max_mat_index + 1;

		return [doMakeMeshForVoxels(voxels, num_mats), subsample_factor];
	}
}
