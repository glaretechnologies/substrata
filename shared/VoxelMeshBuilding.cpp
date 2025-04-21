/*=====================================================================
VoxelMeshBuilding.cpp
---------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "VoxelMeshBuilding.h"


#include "../shared/WorldObject.h"
#include "../dll/include/IndigoException.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/ShouldCancelCallback.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StandardPrintOutput.h"
#include "../utils/HashMapInsertOnly2.h"
#include "../utils/Sort.h"
#include "../utils/Array2D.h"
#include "../utils/Array3D.h"
#if GUI_CLIENT
#include "superluminal/PerformanceAPI.h"
#endif
#include <limits>


class VoxelHashFunc
{
public:
	size_t operator() (const Vec3<int>& v) const
	{
		return hashBytes((const uint8*)&v.x, sizeof(int)*3); // TODO: use better hash func.
	}
};


struct VoxelBounds
{
	Vec3<int> min;
	Vec3<int> max;
};


// Use a uint8 for storing the voxel material index in the 3d-array.  This allows us to handle up to 255 different materials, which seems to be enough for all voxel models seen so far.
// The smaller this type, the less total memory used, and the faster the code runs for large voxel model sizes due to cache effects.
typedef uint8 VoxelMatIndexType;


struct VertPosKeyInt8
{
	uint8 v[3];
	uint8 misc;
	// misc:
	// Bit 7: set for any used key (to distinguish from empty key)
	// Bit 0: Value overflow along x axis (for example we get an overflow if vertex coordinate is 256 for uint8 coords)
	// Bit 1: Value overflow along y axis
	// Bit 2: Value overflow along z axis

	bool operator == (const VertPosKeyInt8& other) const { return v[0] == other.v[0] && v[1] == other.v[1] && v[2] == other.v[2] && misc == other.misc; }
	bool operator != (const VertPosKeyInt8& other) const { return v[0] != other.v[0] || v[1] != other.v[1] || v[2] != other.v[2] || misc != other.misc; }
};


class VertPosKeyInt8HashFunc
{
public:
	size_t operator() (const VertPosKeyInt8& key) const
	{
		return hashBytes((const uint8*)&key.v, sizeof(int8) * 4); // TODO: use better hash func.
	}
};


struct VertPosKeyInt16
{
	uint16 v[3];
	uint16 misc;

	bool operator == (const VertPosKeyInt16& other) const { return v[0] == other.v[0] && v[1] == other.v[1] && v[2] == other.v[2] && misc == other.misc; }
	bool operator != (const VertPosKeyInt16& other) const { return v[0] != other.v[0] || v[1] != other.v[1] || v[2] != other.v[2] || misc != other.misc; }
};


class VertPosKeyInt16HashFunc
{
public:
	size_t operator() (const VertPosKeyInt16& key) const
	{
		return hashBytes((const uint8*)&key.v, sizeof(int16) * 4); // TODO: use better hash func.
	}
};


template <class VertPosKeyType, typename VertPosIntType, class VertPosKeyHashFunc>
static void makeVoxelMeshForVertPosKeyType(const VoxelBounds& bounds_, const Vec3<int>& res_, const Array3D<VoxelMatIndexType>& voxel_array, size_t num_orig_voxels, const js::Vector<bool, 16>& mats_transparent_, Indigo::Mesh* mesh, glare::Allocator* mem_allocator)
{
	const VoxelBounds bounds = bounds_;
	const Vec3<int> res = res_;
	const VoxelMatIndexType no_voxel_mat = std::numeric_limits<VoxelMatIndexType>::max();

	// Build a local array of mat-transparent booleans, one for each material.  If no such entry in mats_transparent_ for a given index, assume opaque.
	bool mat_transparent[256];
	for(size_t i=0; i<256; ++i)
		mat_transparent[i] = (i < mats_transparent_.size()) && mats_transparent_[i];

	// Hash map from voxel coordinates to index of created vertex in mesh->vert_positions.
	// Note that a lot of voxel models create not a lot of vertices.  So don't start the hashmap with too large a size, or it just wastes memory.
	VertPosKeyType vertpos_empty_key;
	vertpos_empty_key.v[0] = vertpos_empty_key.v[1] = vertpos_empty_key.v[2] = 0;
	vertpos_empty_key.misc = 0;
	HashMapInsertOnly2<VertPosKeyType, int, VertPosKeyHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/num_orig_voxels / 100, mem_allocator);


	const int dim_mask_val = (int)std::numeric_limits<VertPosIntType>::max();
	const int dim_num_bits = (dim_mask_val == 255) ? 8 : 16;

	// Bit 7: set for any used key (to distinguish from empty key)
	// Bit 0: Value overflow along x axis (for example we get an overflow if value is 256 for uint8 coords)
	// Bit 1: Value overflow along y axis
	// Bit 2: Value overflow along z axis
	const int default_used_bit = 128;


	// For each dimension (x, y, z)
	for(int dim=0; dim<3; ++dim)
	{
		// Want the a_axis x b_axis = dim_axis
		int dim_a, dim_b;
		if(dim == 0)
		{
			dim_a = 1;
			dim_b = 2;
		}
		else if(dim == 1)
		{
			dim_a = 2;
			dim_b = 0;
		}
		else // dim == 2:
		{
			dim_a = 0;
			dim_b = 1;
		}

		// Get the extents along dim_a, dim_b
		const int a_min = bounds.min[dim_a];
		const int a_size = res[dim_a];

		const int b_min = bounds.min[dim_b];
		const int b_size = res[dim_b];

		// Walk from lower to greater coords, look for downwards facing faces
		const int dim_min = bounds.min[dim];
		const int dim_size = res[dim];

		// An array of voxel faces that still need to be processed.  We store the face material index if the face needs to be processed, and no_voxel_mat otherwise.  Processed = included in a greedy quad already.
		Array2D<VoxelMatIndexType> face_needed_mat(a_size, b_size);

		for(int dim_coord = 0; dim_coord < dim_size; ++dim_coord)
		{
			Vec3<int> vox_indices, adjacent_vox_indices; // pos coords of current voxel, and adjacent voxel

			//================= Do lower faces along dim ==========================
			// Build face_needed_mat data for this slice
			vox_indices[dim] = dim_coord;
			adjacent_vox_indices[dim] = dim_coord - 1;
			for(int y=0; y<b_size; ++y)
			for(int x=0; x<a_size; ++x)
			{
				vox_indices[dim_a] = x;
				vox_indices[dim_b] = y;

				VoxelMatIndexType this_face_needed_mat = no_voxel_mat;
				const auto vox_mat_index = voxel_array.elem(vox_indices.x, vox_indices.y, vox_indices.z);
				if(vox_mat_index != no_voxel_mat) // If there is a voxel here
				{
					adjacent_vox_indices[dim_a] = x;
					adjacent_vox_indices[dim_b] = y;
					if(dim_coord > 0) // If adjacent vox indices are in array bounds: (if dim_coord - 1 >= 0)
					{
						const auto adjacent_vox_mat_index = voxel_array.elem(adjacent_vox_indices.x, adjacent_vox_indices.y, adjacent_vox_indices.z);

						// For an opaque or transparent voxel (the material assigned to it at least), adjacent to an empty voxel, we want to create a face.
						// For an opaque voxel adjacent to another opaque voxel, we don't want to create a face, as it won't be visible.
						// For an opaque voxel adjacent to a transparent voxel, we want to create a single face with the opaque material.
						if((adjacent_vox_mat_index == no_voxel_mat) || // If adjacent voxel is empty, or
							(mat_transparent[adjacent_vox_mat_index] && (adjacent_vox_mat_index != vox_mat_index))) // the adjacent voxel is transparent, and the adjacent voxel has a different material.
							this_face_needed_mat = vox_mat_index;
					}
					else
						this_face_needed_mat = vox_mat_index;
				}

				face_needed_mat.elem(x, y) = this_face_needed_mat;
			}

			// For each voxel face:
			for(int start_y=0; start_y<b_size; ++start_y)
			for(int start_x=0; start_x<a_size; ++start_x)
			{
				const int start_face_needed_mat = face_needed_mat.elem(start_x, start_y);
				if(start_face_needed_mat != no_voxel_mat) // If we need a face here:
				{
					// Start a quad here (start corner at (start_x, start_y))
					// The quad will range from (start_x, start_y) to (end_x, end_y)
					int end_x = start_x + 1;
					int end_y = start_y + 1;

					bool x_increase_ok = true;
					bool y_increase_ok = true;
					while(x_increase_ok || y_increase_ok)
					{
						// Try and increase in x direction
						if(x_increase_ok)
						{
							if(end_x < a_size) // If there is still room to increase in x direction:
							{
								// Check y values for new x = end_x
								for(int y = start_y; y < end_y; ++y)
									if(face_needed_mat.elem(end_x, y) != start_face_needed_mat)
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
							if(end_y < b_size)
							{
								// Check x values for new y = end_y
								for(int x = start_x; x < end_x; ++x)
									if(face_needed_mat.elem(x, end_y) != start_face_needed_mat)
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
					for(int y=start_y; y < end_y; ++y)
					for(int x=start_x; x < end_x; ++x)
						face_needed_mat.elem(x, y) = no_voxel_mat;

					// Add the greedy quad
					unsigned int v_i[4]; // quad vert indices
					Indigo::Vec3f v; // Vertex position coordinates
					v[dim] = (float)(dim_min + dim_coord);

					VertPosKeyType key;
					assert(dim_coord >= 0 && dim_coord <= std::numeric_limits<VertPosIntType>::max());
					key.v[dim] = (VertPosIntType)dim_coord;

					const float start_x_coord = (float)(start_x + a_min);
					const float start_y_coord = (float)(start_y + b_min);
					const float end_x_coord   = (float)(end_x + a_min);
					const float end_y_coord   = (float)(end_y + b_min);

					assert(start_x >= 0 && start_x <= std::numeric_limits<VertPosIntType>::max());
					assert(start_y >= 0 && start_y <= std::numeric_limits<VertPosIntType>::max());
					assert(end_x   >= 0 && end_x   <= std::numeric_limits<VertPosIntType>::max() + 1);
					assert(end_y   >= 0 && end_y   <= std::numeric_limits<VertPosIntType>::max() + 1);

					{
						// bot left
						v[dim_a] = start_x_coord;
						v[dim_b] = start_y_coord;

						key.v[dim_a] = (VertPosIntType)(start_x);
						key.v[dim_b] = (VertPosIntType)(start_y);
						key.misc = default_used_bit;

						// returns object of type std::pair<iterator, bool>
						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size())); // Try and insert vertex
						v_i[0] = insert_res.first->second; // Get existing or new item (insert_res.first) - a (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{
						// top left
						v[dim_a] = start_x_coord;
						v[dim_b] = end_y_coord;

						key.v[dim_a] = (VertPosIntType)start_x;
						key.v[dim_b] = (VertPosIntType)(end_y & dim_mask_val);
						key.misc = (VertPosIntType)(default_used_bit |
							((end_y >> dim_num_bits) << dim_b)
							);
						
						// An example with uint8 coords:
						// end_y >> dim_num_bits will be 0, or 1 if end_y == 256
					

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[1] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{
						// top right
						v[dim_a] = end_x_coord;
						v[dim_b] = end_y_coord;

						key.v[dim_a] = (VertPosIntType)(end_x & dim_mask_val);
						key.v[dim_b] = (VertPosIntType)(end_y & dim_mask_val);
						key.misc = (VertPosIntType)(default_used_bit |
							((end_x >> dim_num_bits) << dim_a) |
							((end_y >> dim_num_bits) << dim_b)
							);

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[2] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{
						// bot right
						v[dim_a] = end_x_coord;
						v[dim_b] = start_y_coord;

						key.v[dim_a] = (VertPosIntType)(end_x & dim_mask_val);
						key.v[dim_b] = (VertPosIntType)start_y;
						key.misc = (VertPosIntType)(
							default_used_bit |
							((end_x >> dim_num_bits) << dim_a)
							);

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[3] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}

					assert(mesh->vert_positions.size() == vertpos_hash.size());

					const size_t tri_start = mesh->triangles.size();
					mesh->triangles.resize(tri_start + 2);

					assert(start_face_needed_mat != no_voxel_mat);

					mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
					mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
					mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
					mesh->triangles[tri_start + 0].uv_indices[0]     = 0;
					mesh->triangles[tri_start + 0].uv_indices[1]     = 0;
					mesh->triangles[tri_start + 0].uv_indices[2]     = 0;
					mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)start_face_needed_mat;

					mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
					mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
					mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
					mesh->triangles[tri_start + 1].uv_indices[0]     = 0;
					mesh->triangles[tri_start + 1].uv_indices[1]     = 0;
					mesh->triangles[tri_start + 1].uv_indices[2]     = 0;
					mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)start_face_needed_mat;
				}
			}

			//================= Do upper faces along dim ==========================
			// Build face_needed data for this slice
			adjacent_vox_indices[dim] = dim_coord + 1;
			for(int y=0; y<b_size; ++y)
			for(int x=0; x<a_size; ++x)
			{
				vox_indices[dim_a] = x;
				vox_indices[dim_b] = y;
				const auto vox_mat_index = voxel_array.elem(vox_indices.x, vox_indices.y, vox_indices.z);

				VoxelMatIndexType this_face_needed_mat = no_voxel_mat;
				if(vox_mat_index != no_voxel_mat) // If there is a voxel here
				{
					adjacent_vox_indices[dim_a] = x;
					adjacent_vox_indices[dim_b] = y;
					if(dim_coord < dim_size - 1) // If adjacent vox indices are in array bounds: (if dim_coord + 1 < dim_size)
					{
						const auto adjacent_vox_mat_index = voxel_array.elem(adjacent_vox_indices.x, adjacent_vox_indices.y, adjacent_vox_indices.z);
						if((adjacent_vox_mat_index == no_voxel_mat) || 
							(mat_transparent[adjacent_vox_mat_index] && (adjacent_vox_mat_index != vox_mat_index)))
							this_face_needed_mat = vox_mat_index;
					}
					else
						this_face_needed_mat = vox_mat_index;
				}
				face_needed_mat.elem(x, y) = this_face_needed_mat;
			}

			// For each voxel face:
			for(int start_y=0; start_y<b_size; ++start_y)
			for(int start_x=0; start_x<a_size; ++start_x)
			{
				const int start_face_needed_mat = face_needed_mat.elem(start_x, start_y);
				if(start_face_needed_mat != no_voxel_mat)
				{
					// Start a quad here (start corner at (start_x, start_y))
					// The quad will range from (start_x, start_y) to (end_x, end_y)
					int end_x = start_x + 1;
					int end_y = start_y + 1;

					bool x_increase_ok = true;
					bool y_increase_ok = true;
					while(x_increase_ok || y_increase_ok)
					{
						// Try and increase in x direction
						if(x_increase_ok)
						{
							if(end_x < a_size) // If there is still room to increase in x direction:
							{
								// Check y values for new x = end_x
								for(int y = start_y; y < end_y; ++y)
									if(face_needed_mat.elem(end_x, y) != start_face_needed_mat)
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
							if(end_y < b_size)
							{
								// Check x values for new y = end_y
								for(int x = start_x; x < end_x; ++x)
									if(face_needed_mat.elem(x, end_y) != start_face_needed_mat)
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
					for(int y=start_y; y < end_y; ++y)
					for(int x=start_x; x < end_x; ++x)
						face_needed_mat.elem(x, y) = no_voxel_mat;

					// Add the greedy quad
					unsigned int v_i[4]; // quad vert indices
					Indigo::Vec3f v;
					v[dim] = (float)(dim_coord + dim_min + 1);

					VertPosKeyType key;
					VertPosIntType dim_initial_used;
					if(dim_coord == std::numeric_limits<VertPosIntType>::max()) // NOTE: for vertices on upper face of entire voxel volume, we ran out of coordinates (e.g. has value 256 for uint8 keys)
					{
						key.v[dim] = 0;
						dim_initial_used = default_used_bit | (1 << dim);
					}
					else
					{
						assert((dim_coord + 1) >= 0 && (dim_coord + 1) <= std::numeric_limits<VertPosIntType>::max());
						key.v[dim] = (VertPosIntType)(dim_coord + 1);
						dim_initial_used = default_used_bit;
					}

					assert(start_x >= 0 && start_x <= std::numeric_limits<VertPosIntType>::max());
					assert(start_y >= 0 && start_y <= std::numeric_limits<VertPosIntType>::max());
					assert(end_x   >= 0 && end_x   <= std::numeric_limits<VertPosIntType>::max() + 1);
					assert(end_y   >= 0 && end_y   <= std::numeric_limits<VertPosIntType>::max() + 1);

					const float start_x_coord = (float)(start_x + a_min);
					const float start_y_coord = (float)(start_y + b_min);
					const float end_x_coord   = (float)(end_x + a_min);
					const float end_y_coord   = (float)(end_y + b_min);

					{ // Add bot left vert
						v[dim_a] = start_x_coord;
						v[dim_b] = start_y_coord;

						key.v[dim_a] = (VertPosIntType)start_x;
						key.v[dim_b] = (VertPosIntType)start_y;
						key.misc = dim_initial_used;

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[0] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{ // bot right
						v[dim_a] = end_x_coord;
						v[dim_b] = start_y_coord;

						key.v[dim_a] = (VertPosIntType)(end_x & dim_mask_val);
						key.v[dim_b] = (VertPosIntType)start_y;
						key.misc = (VertPosIntType)(dim_initial_used |
							((end_x >> dim_num_bits) << dim_a)
							);

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[1] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{ // top right
						v[dim_a] = end_x_coord;
						v[dim_b] = end_y_coord;

						key.v[dim_a] = (VertPosIntType)(end_x & dim_mask_val);
						key.v[dim_b] = (VertPosIntType)(end_y & dim_mask_val);
						key.misc = (VertPosIntType)(dim_initial_used |
							((end_x >> dim_num_bits) << dim_a) |
							((end_y >> dim_num_bits) << dim_b)
							);

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[2] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}
					{ // top left
						v[dim_a] = start_x_coord;
						v[dim_b] = end_y_coord;

						key.v[dim_a] = (VertPosIntType)start_x;
						key.v[dim_b] = (VertPosIntType)(end_y & dim_mask_val);
						key.misc = (VertPosIntType)(dim_initial_used |
							((end_y >> dim_num_bits) << dim_b)
							);

						const auto insert_res = vertpos_hash.insert(std::make_pair(key, (int)vertpos_hash.size()));
						v_i[3] = insert_res.first->second; // deref iterator to get (key, index) pair, then get the index.
						if(insert_res.second) // If inserted new value:
							mesh->vert_positions.push_back(v);
					}

					const size_t tri_start = mesh->triangles.size();
					mesh->triangles.resize(tri_start + 2);

					assert(start_face_needed_mat != no_voxel_mat);

					mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
					mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
					mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
					mesh->triangles[tri_start + 0].uv_indices[0]     = 0;
					mesh->triangles[tri_start + 0].uv_indices[1]     = 0;
					mesh->triangles[tri_start + 0].uv_indices[2]     = 0;
					mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)start_face_needed_mat;

					mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
					mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
					mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
					mesh->triangles[tri_start + 1].uv_indices[0]     = 0;
					mesh->triangles[tri_start + 1].uv_indices[1]     = 0;
					mesh->triangles[tri_start + 1].uv_indices[2]     = 0;
					mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)start_face_needed_mat;
				}
			}
		}

		//conPrint("Dim " + toString(dim) + " took " + dim_timer.elapsedStringNSigFigs(4));
	} // End for each dim
}





// Does greedy meshing.
// Splats voxels to 3d array.
static Reference<Indigo::Mesh> doMakeIndigoMeshForVoxelGroupWith3dArray(const glare::AllocatorVector<Voxel, 16>& voxels, int subsample_factor, const js::Vector<bool, 16>& mats_transparent_, glare::Allocator* mem_allocator)
{
#if GUI_CLIENT
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();
#endif

	try
	{
		if(voxels.empty())
			throw glare::Exception("No voxels");

		Reference<Indigo::Mesh> mesh = new Indigo::Mesh();

		// We won't reserve large numbers of vertex positions and triangles here, since sometimes the number of resulting verts and tris can be pretty small.
		//mesh->vert_positions.reserve(voxels.size());
		//mesh->triangles.reserve(voxels.size());

		mesh->setMaxNumTexcoordSets(0);

		const size_t voxels_size = voxels.size();

		// Do a pass over the voxels to get the bounds
		Vec4i bounds_min(std::numeric_limits<int>::max());
		Vec4i bounds_max(std::numeric_limits<int>::min());
		int max_mat_index = 0;
		const int subsample_shift_amount_b = Maths::intLogBase2((uint32)subsample_factor);
		for(size_t i=0; i<voxels_size; ++i)
		{
			const Vec4i orig_vox_pos = Vec4i(voxels[i].pos.x, voxels[i].pos.y, voxels[i].pos.z, 0);
			const Vec4i vox_pos = shiftRightWithSignExtension(orig_vox_pos, subsample_shift_amount_b); // Shifting right with sign extension effectively divides by 2^subsample_shift_amount_b, rounding down.

			bounds_min = min(bounds_min, vox_pos);
			bounds_max = max(bounds_max, vox_pos);
			if(voxels[i].mat_index < 0)
				throw glare::Exception("Invalid mat index (< 0)");
			max_mat_index = myMax((int)voxels[i].mat_index, max_mat_index);
		}

		// We want to be able to fit all the material indices, plus the 'no voxel' index, into the 256 values of a uint8.  So mat_index of 255 = no voxel index.
		if(max_mat_index >= 255) 
			throw glare::Exception("Too many materials");

		VoxelBounds bounds;
		bounds.min = Vec3<int>(bounds_min[0], bounds_min[1], bounds_min[2]);
		bounds.max = Vec3<int>(bounds_max[0], bounds_max[1], bounds_max[2]);

		// Limit voxel coordinates to something reasonable.  Also avoids integer overflows in the res computation below.
		// Limit to 16-bit values, so we don't have to handle float vertex coords and can just use GL_BYTE and GL_SHORT.
		const int min_coord = -32768;
		const int max_coord =  32766; // max voxel coord is 32766, resulting in max vert coord of 32767
		if(bounds_min[0] < min_coord || bounds_min[1] < min_coord || bounds_min[2] < min_coord)
			throw glare::Exception("Invalid voxel position coord: " + bounds_min.toString());
		if(bounds_max[0] > max_coord || bounds_max[1] > max_coord || bounds_max[2] > max_coord)
			throw glare::Exception("Invalid voxel position coord: " + bounds_max.toString());
	
		// Do a pass over the voxels to splat into a 3d array
		const Vec3<int> res = bounds.max - bounds.min + Vec3<int>(1); // Voxel array resolution

		const int max_dim_w = 100000;
		if(res.x > max_dim_w || res.y > max_dim_w || res.z > max_dim_w)
			throw glare::Exception("Voxel dimension span exceeds " + toString(max_dim_w));

		const int64 voxel_array_size = (int64)res.x * (int64)res.y * (int64)res.z; // Use int64 to avoid overflow.
		const int64 max_voxel_array_size = (1 << 26) / sizeof(VoxelMatIndexType); // 64 MB, ~64 million voxels
		if(voxel_array_size > max_voxel_array_size)
			throw glare::Exception("Voxel array num voxels (" + toString(voxel_array_size) + ") exceeds limit of " + toString(max_voxel_array_size));

		const VoxelMatIndexType no_voxel_mat = std::numeric_limits<VoxelMatIndexType>::max();
		Array3D<VoxelMatIndexType> voxel_array(res.x, res.y, res.z, no_voxel_mat, mem_allocator);

		for(size_t i=0; i<voxels_size; ++i)
		{
			const Voxel& voxel = voxels[i];
			const Vec4i orig_vox_pos = Vec4i(voxels[i].pos.x, voxels[i].pos.y, voxels[i].pos.z, 0);
			const Vec4i vox_pos = shiftRightWithSignExtension(orig_vox_pos, subsample_shift_amount_b); // Shifting right with sign extension effectively divides by 2^subsample_shift_amount_b, rounding down.
			const Vec4i indices = vox_pos - bounds_min;
			voxel_array.elem(indices[0], indices[1], indices[2]) = (VoxelMatIndexType)voxel.mat_index;
		}

		//if(voxel_array.getData().size() > 100000)
		//	conPrint("voxel_array size: " + toString(voxel_array.getData().size()) + " elems, " + toString(voxel_array.getData().dataSizeBytes()) + " B");

		if(res[0] <= 256 && res[1] <= 256 && res[2] <= 256)
		{
			makeVoxelMeshForVertPosKeyType<VertPosKeyInt8, uint8, VertPosKeyInt8HashFunc>(bounds, res, voxel_array, voxels.size(), mats_transparent_, mesh.ptr(), mem_allocator);
		}
		else
		{
			makeVoxelMeshForVertPosKeyType<VertPosKeyInt16, uint16, VertPosKeyInt16HashFunc>(bounds, res, voxel_array, voxels.size(), mats_transparent_, mesh.ptr(), mem_allocator);
		}

		mesh->endOfModel();
		assert(isFinite(mesh->aabb_os.bound[0].x));
		return mesh;
	}
	catch(Indigo::IndigoException& e)
	{
		throw glare::Exception(toStdString(e.what()));
	}
}


struct VoxelVertInfo
{
	VoxelVertInfo() {}
	VoxelVertInfo(const Indigo::Vec3f& pos_, const Indigo::Vec3f& normal_) : pos(pos_), normal(normal_) {}

	inline bool operator == (const VoxelVertInfo& other) const { return pos == other.pos && normal == other.normal; }
	inline bool operator != (const VoxelVertInfo& other) const { return pos != other.pos || normal != other.normal; }

	Indigo::Vec3f pos;
	Indigo::Vec3f normal;
};


struct VoxelVertInfoHashFunc
{
	size_t operator() (const VoxelVertInfo& v) const
	{
		return hashBytes((const uint8*)&v.pos.x, sizeof(Indigo::Vec3f)); // TODO: use better hash func.
	}
};


// Does greedy meshing
// Computes vertex normals, thereby avoiding reusing vertices with the same positions but different normals.
Reference<Indigo::Mesh> VoxelMeshBuilding::makeIndigoMeshWithShadingNormalsForVoxelGroup(const VoxelGroup& voxel_group, const int subsample_factor, const js::Vector<bool, 16>& mats_transparent,
		glare::Allocator* mem_allocator)
{
	const glare::AllocatorVector<Voxel, 16>& voxels = voxel_group.voxels;
	HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc> voxel_hash(/*empty key=*/Vec3<int>(1000000));

	int max_mat_index = 0;
	for(size_t i=0; i<voxels.size(); ++i)
	{
		const Vec3i p(voxels[i].pos.x / subsample_factor, voxels[i].pos.y / subsample_factor, voxels[i].pos.z / subsample_factor);
		voxel_hash[p] = voxels[i].mat_index;
		max_mat_index = myMax(max_mat_index, (int)voxels[i].mat_index);
	}

	const int num_mats = max_mat_index + 1;


	Reference<Indigo::Mesh> mesh = new Indigo::Mesh();

	VoxelVertInfo vertpos_empty_key;
	vertpos_empty_key.pos = Indigo::Vec3f(std::numeric_limits<float>::max());
	vertpos_empty_key.normal = Indigo::Vec3f(std::numeric_limits<float>::max());
	HashMapInsertOnly2<VoxelVertInfo, int, VoxelVertInfoHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/voxels.size());

	mesh->vert_positions.reserve(voxels.size());
	mesh->vert_normals.reserve(voxels.size());
	mesh->uv_pairs.reserve(voxels.size());
	mesh->triangles.reserve(voxels.size());

	mesh->setMaxNumTexcoordSets(1);

	VoxelBounds b;
	b.min = Vec3<int>( 1000000000);
	b.max = Vec3<int>(-1000000000);
	std::vector<VoxelBounds> mat_vox_bounds(num_mats, b);
	for(size_t i=0; i<voxels.size(); ++i) // For each mat
	{
		const Vec3i p(voxels[i].pos.x / subsample_factor, voxels[i].pos.y / subsample_factor, voxels[i].pos.z / subsample_factor);

		const int mat_index = voxels[i].mat_index;
		mat_vox_bounds[mat_index].min = mat_vox_bounds[mat_index].min.min(p);
		mat_vox_bounds[mat_index].max = mat_vox_bounds[mat_index].max.max(p);
	}

	for(size_t mat_i=0; mat_i<num_mats; ++mat_i) // For each mat
	{
		if(mat_vox_bounds[mat_i].min == Vec3<int>(1000000000))
			continue; // No voxels for this mat.

		// For each dimension (x, y, z)
		for(int dim=0; dim<3; ++dim)
		{
			// Want the a_axis x b_axis = dim_axis
			int dim_a, dim_b;
			if(dim == 0)
			{
				dim_a = 1;
				dim_b = 2;
			}
			else if(dim == 1)
			{
				dim_a = 2;
				dim_b = 0;
			}
			else // dim == 2:
			{
				dim_a = 0;
				dim_b = 1;
			}

			Indigo::Vec3f normal_up(0.f);
			normal_up[dim] = 1.f;
			Indigo::Vec3f normal_down(0.f);
			normal_down[dim] = -1.f;

			// Get the extents along dim_a, dim_b
			const int a_min = mat_vox_bounds[mat_i].min[dim_a];
			const int a_end = mat_vox_bounds[mat_i].max[dim_a] + 1;

			const int b_min = mat_vox_bounds[mat_i].min[dim_b];
			const int b_end = mat_vox_bounds[mat_i].max[dim_b] + 1;

			// Walk from lower to greater coords, look for downwards facing faces
			const int dim_min = mat_vox_bounds[mat_i].min[dim];
			const int dim_end = mat_vox_bounds[mat_i].max[dim] + 1;

			// Make an array to indicate processed voxel faces.  Processed = included in a greedy quad already.
			Array2D<bool> vox_present(a_end - a_min, b_end - b_min); // Memorize the voxel lookup to use for building upper faces
			Array2D<bool> face_needed(a_end - a_min, b_end - b_min);

			for(int dim_coord = dim_min; dim_coord < dim_end; ++dim_coord)
			{
				//================= Do lower faces along dim ==========================
				// Build face_needed data for this slice
				Vec3<int> vox, adjacent_vox_pos;
				vox[dim] = dim_coord;
				adjacent_vox_pos[dim] = dim_coord - 1;
				for(int y=b_min; y<b_end; ++y)
				for(int x=a_min; x<a_end; ++x)
				{
					vox[dim_a] = x;
					vox[dim_b] = y;

					bool this_face_needed = false;
					bool this_vox_present = false;
					auto res = voxel_hash.find(vox);
					if((res != voxel_hash.end()) && (res->second == mat_i)) // If there is a voxel here with mat_i
					{
						this_vox_present = true;

						adjacent_vox_pos[dim_a] = x;
						adjacent_vox_pos[dim_b] = y;
						auto adjacent_res = voxel_hash.find(adjacent_vox_pos);
						if((adjacent_res == voxel_hash.end()) || (adjacent_res->second != mat_i)) // If there is no adjacent voxel, or the adjacent voxel has a different material:
							this_face_needed = true;
					}
					vox_present.elem(x - a_min, y - b_min) = this_vox_present;
					face_needed.elem(x - a_min, y - b_min) = this_face_needed;
				}

				// For each voxel face:
				for(int start_y=b_min; start_y<b_end; ++start_y)
				for(int start_x=a_min; start_x<a_end; ++start_x)
				{
					if(face_needed.elem(start_x - a_min, start_y - b_min)) // If we need a face here:
					{
						// Start a quad here (start corner at (start_x, start_y))
						// The quad will range from (start_x, start_y) to (end_x, end_y)
						int end_x = start_x + 1;
						int end_y = start_y + 1;

						bool x_increase_ok = true;
						bool y_increase_ok = true;
						while(x_increase_ok || y_increase_ok)
						{
							// Try and increase in x direction
							if(x_increase_ok)
							{
								if(end_x < a_end) // If there is still room to increase in x direction:
								{
									// Check y values for new x = end_x
									for(int y = start_y; y < end_y; ++y)
										if(!face_needed.elem(end_x - a_min, y - b_min))
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
									for(int x = start_x; x < end_x; ++x)
										if(!face_needed.elem(x - a_min, end_y - b_min))
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
						for(int y=start_y; y < end_y; ++y)
						for(int x=start_x; x < end_x; ++x)
							face_needed.elem(x - a_min, y - b_min) = false;

						// Add the greedy quad
						unsigned int v_i[4]; // quad vert indices
						Indigo::Vec3f v;
						v[dim] = (float)dim_coord;
						{
							// bot left
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)start_y;

							// returns object of type std::pair<iterator, bool>
							VoxelVertInfo vert_info(v, normal_down);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size())); // Try and insert vertex
							v_i[0] = insert_res.first->second; // Get existing or new item (insert_res.first) - a (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_down);
								mesh->uv_pairs.push_back(
									(dim == 0)  ? Indigo::Vec2f(-(float)start_x, (float)start_y) : 
									((dim == 1) ? Indigo::Vec2f( (float)start_y, (float)start_x) : 
									              Indigo::Vec2f(-(float)start_x, (float)start_y)));
							}
						}
						{
							// top left
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)end_y;

							VoxelVertInfo vert_info(v, normal_down);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[1] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_down);
								mesh->uv_pairs.push_back(
									(dim == 0) ? Indigo::Vec2f(-(float)start_x, (float)end_y) : 
									(dim == 1) ? Indigo::Vec2f( (float)end_y, (float)start_x) : 
									             Indigo::Vec2f(-(float)start_x, (float)end_y));
							}
						}
						{
							// top right
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)end_y;

							VoxelVertInfo vert_info(v, normal_down);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[2] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_down);
								mesh->uv_pairs.push_back(
									(dim == 0) ? Indigo::Vec2f(-(float)end_x, (float)end_y) :
									(dim == 1) ? Indigo::Vec2f( (float)end_y, (float)end_x) : 
									             Indigo::Vec2f(-(float)end_x, (float)end_y));
							}
						}
						{
							// bot right
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)start_y;

							VoxelVertInfo vert_info(v, normal_down);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[3] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_down);
								mesh->uv_pairs.push_back(
									(dim == 0) ? Indigo::Vec2f(-(float)end_x, (float)start_y) :
									(dim == 1) ? Indigo::Vec2f( (float)start_y, (float)end_x) : 
									             Indigo::Vec2f(-(float)end_x, (float)start_y));
							}
						}

						assert(mesh->vert_positions.size() == vertpos_hash.size());

						const size_t tri_start = mesh->triangles.size();
						mesh->triangles.resize(tri_start + 2);
						
						mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
						mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
						mesh->triangles[tri_start + 0].uv_indices[0]     = v_i[0];
						mesh->triangles[tri_start + 0].uv_indices[1]     = v_i[1];
						mesh->triangles[tri_start + 0].uv_indices[2]     = v_i[2];
						mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)mat_i;
						
						mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
						mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
						mesh->triangles[tri_start + 1].uv_indices[0]     = v_i[0];
						mesh->triangles[tri_start + 1].uv_indices[1]     = v_i[2];
						mesh->triangles[tri_start + 1].uv_indices[2]     = v_i[3];
						mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)mat_i;
					}
				}

				//================= Do upper faces along dim ==========================
				// Build face_needed data for this slice
				adjacent_vox_pos[dim] = dim_coord + 1;
				for(int y=b_min; y<b_end; ++y)
				for(int x=a_min; x<a_end; ++x)
				{
					bool this_face_needed = false;
					if(vox_present.elem(x - a_min, y - b_min)) // If there is a voxel here with mat_i
					{
						adjacent_vox_pos[dim_a] = x;
						adjacent_vox_pos[dim_b] = y;
						auto adjacent_res = voxel_hash.find(adjacent_vox_pos);
						if((adjacent_res == voxel_hash.end()) || (adjacent_res->second != mat_i)) // If there is no adjacent voxel, or the adjacent voxel has a different material:
							this_face_needed = true;
					}
					face_needed.elem(x - a_min, y - b_min) = this_face_needed;
				}

				// For each voxel face:
				for(int start_y=b_min; start_y<b_end; ++start_y)
				for(int start_x=a_min; start_x<a_end; ++start_x)
				{
					if(face_needed.elem(start_x - a_min, start_y - b_min))
					{
						// Start a quad here (start corner at (start_x, start_y))
						// The quad will range from (start_x, start_y) to (end_x, end_y)
						int end_x = start_x + 1;
						int end_y = start_y + 1;

						bool x_increase_ok = true;
						bool y_increase_ok = true;
						while(x_increase_ok || y_increase_ok)
						{
							// Try and increase in x direction
							if(x_increase_ok)
							{
								if(end_x < a_end) // If there is still room to increase in x direction:
								{
									// Check y values for new x = end_x
									for(int y = start_y; y < end_y; ++y)
										if(!face_needed.elem(end_x - a_min, y - b_min))
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
									for(int x = start_x; x < end_x; ++x)
										if(!face_needed.elem(x - a_min, end_y - b_min))
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
						for(int y=start_y; y < end_y; ++y)
							for(int x=start_x; x < end_x; ++x)
								face_needed.elem(x - a_min, y - b_min) = false;

						const float quad_dim_coord = (float)(dim_coord + 1);

						// Add the greedy quad
						unsigned int v_i[4]; // quad vert indices
						Indigo::Vec3f v;
						v[dim] = (float)quad_dim_coord;
						{ // Add bot left vert
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)start_y;
								
							VoxelVertInfo vert_info(v, normal_up);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[0] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_up);
								mesh->uv_pairs.push_back((dim == 1) ? 
									Indigo::Vec2f(-(float)start_y, (float)start_x) : 
									Indigo::Vec2f((float)start_x, (float)start_y));
							}
						}
						{ // bot right
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)start_y;

							VoxelVertInfo vert_info(v, normal_up);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[1] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_up);
								mesh->uv_pairs.push_back((dim == 1) ? 
									Indigo::Vec2f(-(float)start_y, (float)end_x) : 
									Indigo::Vec2f((float)end_x, (float)start_y));
							}
						}
						{ // top right
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)end_y;

							VoxelVertInfo vert_info(v, normal_up);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[2] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_up);
								mesh->uv_pairs.push_back((dim == 1) ? 
									Indigo::Vec2f(-(float)end_y, (float)end_x) : 
									Indigo::Vec2f((float)end_x, (float)end_y));
							}
						}
						{ // top left
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)end_y;

							VoxelVertInfo vert_info(v, normal_up);
							const auto insert_res = vertpos_hash.insert(std::make_pair(vert_info, (int)vertpos_hash.size()));
							v_i[3] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
							{
								mesh->vert_positions.push_back(v);
								mesh->vert_normals.push_back(normal_up);
								mesh->uv_pairs.push_back((dim == 1) ? 
									Indigo::Vec2f(-(float)end_y, (float)start_x) : 
									Indigo::Vec2f((float)start_x, (float)end_y));
							}
						}
							
						const size_t tri_start = mesh->triangles.size();
						mesh->triangles.resize(tri_start + 2);
							
						mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
						mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
						mesh->triangles[tri_start + 0].uv_indices[0]     = v_i[0];
						mesh->triangles[tri_start + 0].uv_indices[1]     = v_i[1];
						mesh->triangles[tri_start + 0].uv_indices[2]     = v_i[2];
						mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)mat_i;

						mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
						mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
						mesh->triangles[tri_start + 1].uv_indices[0]     = v_i[0];
						mesh->triangles[tri_start + 1].uv_indices[1]     = v_i[2];
						mesh->triangles[tri_start + 1].uv_indices[2]     = v_i[3];
						mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)mat_i;
					}
				}
			}
		}
	}

	mesh->endOfModel();
	assert(isFinite(mesh->aabb_os.bound[0].x));
	return mesh;
}


Reference<Indigo::Mesh> VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group, const int subsample_factor, const js::Vector<bool, 16>& mats_transparent,
	glare::Allocator* mem_allocator)
{
	assert(voxel_group.voxels.size() > 0);
	// conPrint("Adding " + toString(voxel_group.voxels.size()) + " voxels.");

	return doMakeIndigoMeshForVoxelGroupWith3dArray(voxel_group.voxels, subsample_factor, mats_transparent, mem_allocator);
}


#if BUILD_TESTS


#if 0
// Command line:
// C:\fuzz_corpus\voxel_data N:\new_cyberspace\trunk\testfiles\voxels

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		VoxelGroup group;
		group.voxels.resize(size / sizeof(Voxel));
		std::memcpy(group.voxels.data(), data, group.voxels.dataSizeBytes());

		doMakeIndigoMeshForVoxelGroupWith3dArray(group.voxels, 1);
		doMakeIndigoMeshForVoxelGroupWith3dArray(group.voxels, 2);
		doMakeIndigoMeshForVoxelGroupWith3dArray(group.voxels, 4);
	}
	catch(glare::Exception&)
	{
	}

	return 0;  // Non-zero return values are reserved for future use.
}
#endif


#include <simpleraytracer/raymesh.h>
#include <graphics/BatchedMesh.h>
#include <utils/TaskManager.h>
#include <utils/TestUtils.h>


void VoxelMeshBuilding::test()
{
	conPrint("VoxelMeshBuilding::test()");

	glare::TaskManager task_manager;

	// Test a single voxel
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
		testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(0,0,0));
		testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(1,1,1));

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
		testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(0,0,0));
		testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(1,1,1));
	}

	// Test a single voxel not at (0,0,0)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(1, 2, 3), 0));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
		testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(1,2,3));
		testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(2,3,4));

		// Test with subsampling
		//data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, /*generate_shading_normals=*/false);
		//testAssert(data->num_materials_referenced == 1);
		//testAssert(data->triangles.size() == 6 * 2);
		//testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(0,0,0));
		//testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(1,1,1));
	}

	// Test several separated voxels
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(10, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(20, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(30, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(40, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(50, 0, 1), 1));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 6 * 6 * 2);
		testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(0,0,0));
		testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(51,1,2));

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 6 * 6 * 2);
		testAssert(data->aabb_os.bound[0] == Indigo::Vec3f(0,0,0));
		testAssert(data->aabb_os.bound[1] == Indigo::Vec3f(50/4 + 1, 1, 2/4 + 1));
	}

	

	// Test two adjacent voxels with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels (along y axis) with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 1, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels (along z axis) with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		js::Vector<bool, 16> mat_transparent;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/2, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels with different opaque materials.  The faces between the voxels should not be added.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 1));

		js::Vector<bool, 16> mat_transparent(2, false);

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 2 * 5 * 2); // Each voxel should have 5 faces (face between 2 voxels is not added),  * 2 voxels, * 2 triangles/face

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced <= 2);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels - one with an opaque mat, one with a transparent mat.  Exactly 1 face between the voxels should be added, with the opaque material (mat 0).
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 1));

		js::Vector<bool, 16> mat_transparent(2, false);
		mat_transparent[1] = true;

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 11 * 2); // Each voxel should have 5 faces, plus the face in the middle, * 2 triangles/face.

		size_t num_tris_with_mat_0 = 0;
		for(size_t i=0; i<data->triangles.size(); ++i)
			if(data->triangles[i].tri_mat_index == 0)
				num_tris_with_mat_0++;

		testAssert(num_tris_with_mat_0 == 6 * 2);

		// Test with subsampling
		data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/4, mat_transparent, /*allocator=*/NULL);
		testAssert(data->num_materials_referenced <= 2);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test voxels at edge of 256^3 region.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 255), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 255, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 255, 255), 0));
		group.voxels.push_back(Voxel(Vec3<int>(255, 255, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(255, 255, 255), 0));
		group.voxels.push_back(Voxel(Vec3<int>(255, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(255, 0, 255), 0));

		js::Vector<bool, 16> mat_transparent(1, false);

		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

		testAssert(data->num_materials_referenced == 1);
		testAssert(data->vert_positions.size() == 8 * 8); // 8 voxels * 8 vertices (corners) / voxel
		testAssert(data->triangles.size() == 8 * 6 * 2); // 8 voxels * 6 faces/voxel * 2 triangles/face.
	}

	

	// Performance test
	if(false)
	{
		try
		{

			// Object with UID 158939: game console in gallery near central square.  8292300 voxels, but only makes 2485 vertices and 4500 triangles.
			// 169166: roof of voxel house, 951327 voxels, makes 3440 vertices, 5662 triangles
			// 169202: voxel tree, 108247 voxels, 75756 vertices, 124534 tris
			{
				VoxelGroup group;

				{
					std::vector<uint8> filecontents;
					FileUtils::readEntireFile("D:\\files\\voxeldata\\ob_158939_voxeldata.voxdata", filecontents);
					group.voxels.resize(filecontents.size() / sizeof(Voxel));
					testAssert(filecontents.size() == group.voxels.dataSizeBytes());
					std::memcpy(group.voxels.data(), filecontents.data(), filecontents.size());
				}

				printVar(MemAlloc::getHighWaterMarkB());

				conPrint("AABB: " + group.getAABB().toString());
				conPrint("AABB volume: " + toString(group.getAABB().volume()));

				js::Vector<bool, 16> mat_transparent;

				for(int i=0; i<1000; ++i)
				{
					Timer timer;

					Reference<Indigo::Mesh> indigo_mesh = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

					conPrint("Meshing of " + toString(group.voxels.size()) + " voxels with subsample_factor=1 took " + timer.elapsedString());
					conPrint("Resulting num tris: " + toString(indigo_mesh->triangles.size()));

					//BatchedMeshRef batched_mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
					//batched_mesh->writeToFile("D:\\files\\voxeldata\\ob_169202_voxel.bmesh");

					printVar(MemAlloc::getHighWaterMarkB());
				}

				{
					Timer timer;

					Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/2, mat_transparent, /*allocator=*/NULL);

					conPrint("Meshing of " + toString(group.voxels.size()) + " voxels with subsample_factor=2 took " + timer.elapsedString());
					conPrint("Resulting num tris: " + toString(data->triangles.size()));
				}
			}

			if(false)
			{
				js::Vector<bool, 16> mat_transparent;

				VoxelGroup group;
				for(int z=0; z<100; z += 2)
					for(int y=0; y<100; ++y)
						for(int x=0; x<10; ++x)
							group.voxels.push_back(Voxel(Vec3<int>(x, y, z), 0));


				Timer timer;

				Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/1, mat_transparent, /*allocator=*/NULL);

				conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
				conPrint("Resulting num tris: " + toString(data->triangles.size()));
			}
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}
	}

	conPrint("VoxelMeshBuilding::test() done.");
}


#endif // BUILD_TESTS
