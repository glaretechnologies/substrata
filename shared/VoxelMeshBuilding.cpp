/*=====================================================================
VoxelMeshBuilding.cpp
---------------------
Copyright Glare Technologies Limited 2020 -
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
#include <limits>


class VoxelHashFunc
{
public:
	size_t operator() (const Vec3<int>& v) const
	{
		return hashBytes((const uint8*)&v.x, sizeof(int)*3); // TODO: use better hash func.
	}
};


class Vec3fHashFunc
{
public:
	size_t operator() (const Indigo::Vec3f& v) const
	{
		return hashBytes((const uint8*)&v.x, sizeof(Indigo::Vec3f)); // TODO: use better hash func.
	}
};


struct VoxelBuildInfo
{
	int face_offset; // number of faces added before this voxel.
	int num_faces; // num faces added for this voxel.
};


struct GetMatIndex
{
	size_t operator() (const Voxel& v)
	{
		return (size_t)v.mat_index;
	}
};


struct VoxelBounds
{
	Vec3<int> min;
	Vec3<int> max;
};


// Does greedy meshing
static Reference<Indigo::Mesh> doMakeIndigoMeshForVoxelGroup(const std::vector<Voxel>& voxels, const size_t num_mats, const HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc>& voxel_hash)
{
	const size_t num_voxels = voxels.size();

	Reference<Indigo::Mesh> mesh = new Indigo::Mesh();

	const Indigo::Vec3f vertpos_empty_key(std::numeric_limits<float>::max());
	HashMapInsertOnly2<Indigo::Vec3f, int, Vec3fHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/num_voxels);

	mesh->vert_positions.reserve(voxels.size());
	mesh->triangles.reserve(voxels.size());

	mesh->setMaxNumTexcoordSets(0);

	VoxelBounds b;
	b.min = Vec3<int>( 1000000000);
	b.max = Vec3<int>(-1000000000);
	std::vector<VoxelBounds> mat_vox_bounds(num_mats, b);
	for(size_t i=0; i<voxels.size(); ++i) // For each mat
	{
		const int mat_index = voxels[i].mat_index;
		mat_vox_bounds[mat_index].min = mat_vox_bounds[mat_index].min.min(voxels[i].pos);
		mat_vox_bounds[mat_index].max = mat_vox_bounds[mat_index].max.max(voxels[i].pos);
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

			// Get the extents along dim_a, dim_b
			const int a_min = mat_vox_bounds[mat_i].min[dim_a];
			const int a_end = mat_vox_bounds[mat_i].max[dim_a] + 1;

			const int b_min = mat_vox_bounds[mat_i].min[dim_b];
			const int b_end = mat_vox_bounds[mat_i].max[dim_b] + 1;

			// Walk from lower to greater coords, look for downwards facing faces
			const int dim_min = mat_vox_bounds[mat_i].min[dim];
			const int dim_end = mat_vox_bounds[mat_i].max[dim] + 1;

			// Make a map to indicate processed voxel faces.  Processed = included in a greedy quad already.
			Array2D<bool> face_needed(a_end - a_min, b_end - b_min);

			for(int dim_coord = dim_min; dim_coord < dim_end; ++dim_coord)
			{
				// Build face_needed data for this slice
				for(int y=b_min; y<b_end; ++y)
				for(int x=a_min; x<a_end; ++x)
				{
					Vec3<int> vox;
					vox[dim] = dim_coord;
					vox[dim_a] = x;
					vox[dim_b] = y;

					bool this_face_needed = false;
					auto res = voxel_hash.find(vox);
					if((res != voxel_hash.end()) && (res->second == mat_i)) // If there is a voxel here with mat_i
					{
						Vec3<int> adjacent_vox_pos = vox;
						adjacent_vox_pos[dim]--;
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
						unsigned int v_i[4];
						{
							Indigo::Vec3f v; // bot left
							v[dim] = (float)dim_coord;
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)start_y;

							const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
							v_i[0] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
								mesh->vert_positions.push_back(v);
						}
						{
							Indigo::Vec3f v; // top left
							v[dim] = (float)dim_coord;
							v[dim_a] = (float)start_x;
							v[dim_b] = (float)end_y;

							const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
							v_i[1] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
								mesh->vert_positions.push_back(v);
						}
						{
							Indigo::Vec3f v; // top right
							v[dim] = (float)dim_coord;
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)end_y;

							const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
							v_i[2] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
								mesh->vert_positions.push_back(v);
						}
						{
							Indigo::Vec3f v; // bot right
							v[dim] = (float)dim_coord;
							v[dim_a] = (float)end_x;
							v[dim_b] = (float)start_y;

							const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
							v_i[3] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
							if(insert_res.second) // If inserted new value:
								mesh->vert_positions.push_back(v);
						}

						assert(mesh->vert_positions.size() == vertpos_hash.size());

						const size_t tri_start = mesh->triangles.size();
						mesh->triangles.resize(tri_start + 2);
						
						mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
						mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
						mesh->triangles[tri_start + 0].uv_indices[0]     = 0;
						mesh->triangles[tri_start + 0].uv_indices[1]     = 0;
						mesh->triangles[tri_start + 0].uv_indices[2]     = 0;
						mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)mat_i;
						
						mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
						mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
						mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
						mesh->triangles[tri_start + 1].uv_indices[0]     = 0;
						mesh->triangles[tri_start + 1].uv_indices[1]     = 0;
						mesh->triangles[tri_start + 1].uv_indices[2]     = 0;
						mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)mat_i;
					}
				}

				//================= Do upper faces along dim ==========================
				// Build face_needed data for this slice
				for(int y=b_min; y<b_end; ++y)
				for(int x=a_min; x<a_end; ++x)
				{
					Vec3<int> vox;
					vox[dim] = dim_coord;
					vox[dim_a] = x;
					vox[dim_b] = y;

					bool this_face_needed = false;
					auto res = voxel_hash.find(vox);
					if((res != voxel_hash.end()) && (res->second == mat_i)) // If there is a voxel here with mat_i
					{
						Vec3<int> adjacent_vox_pos = vox;
						adjacent_vox_pos[dim]++;
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

						if(end_x > start_x && end_y > start_y)
						{
							const float quad_dim_coord = (float)(dim_coord + 1);

							// Add the greedy quad
							unsigned int v_i[4];
							{
								Indigo::Vec3f v; // bot left
								v[dim] = (float)quad_dim_coord;
								v[dim_a] = (float)start_x;
								v[dim_b] = (float)start_y;
								
								const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
								v_i[0] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
								if(insert_res.second) // If inserted new value:
									mesh->vert_positions.push_back(v);
							}
							{
								Indigo::Vec3f v; // bot right
								v[dim] = (float)quad_dim_coord;
								v[dim_a] = (float)end_x;
								v[dim_b] = (float)start_y;

								const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
								v_i[1] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
								if(insert_res.second) // If inserted new value:
									mesh->vert_positions.push_back(v);
							}
							{
								Indigo::Vec3f v; // top right
								v[dim] = (float)quad_dim_coord;
								v[dim_a] = (float)end_x;
								v[dim_b] = (float)end_y;

								const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
								v_i[2] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
								if(insert_res.second) // If inserted new value:
									mesh->vert_positions.push_back(v);
							}
							{
								Indigo::Vec3f v; // top left
								v[dim] = (float)quad_dim_coord;
								v[dim_a] = (float)start_x;
								v[dim_b] = (float)end_y;

								const auto insert_res = vertpos_hash.insert(std::make_pair(v, (int)vertpos_hash.size()));
								v_i[3] = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
								if(insert_res.second) // If inserted new value:
									mesh->vert_positions.push_back(v);
							}
							
							const size_t tri_start = mesh->triangles.size();
							mesh->triangles.resize(tri_start + 2);
							
							mesh->triangles[tri_start + 0].vertex_indices[0] = v_i[0];
							mesh->triangles[tri_start + 0].vertex_indices[1] = v_i[1];
							mesh->triangles[tri_start + 0].vertex_indices[2] = v_i[2];
							mesh->triangles[tri_start + 0].uv_indices[0]     = 0;
							mesh->triangles[tri_start + 0].uv_indices[1]     = 0;
							mesh->triangles[tri_start + 0].uv_indices[2]     = 0;
							mesh->triangles[tri_start + 0].tri_mat_index     = (uint32)mat_i;

							mesh->triangles[tri_start + 1].vertex_indices[0] = v_i[0];
							mesh->triangles[tri_start + 1].vertex_indices[1] = v_i[2];
							mesh->triangles[tri_start + 1].vertex_indices[2] = v_i[3];
							mesh->triangles[tri_start + 1].uv_indices[0]     = 0;
							mesh->triangles[tri_start + 1].uv_indices[1]     = 0;
							mesh->triangles[tri_start + 1].uv_indices[2]     = 0;
							mesh->triangles[tri_start + 1].tri_mat_index     = (uint32)mat_i;
						}
					}
				}
			}
		}
	}

	mesh->endOfModel();
	return mesh;
}


Reference<Indigo::Mesh> VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group, Vec3<int>& minpos_out, Vec3<int>& maxpos_out)
{
	const size_t num_voxels = voxel_group.voxels.size();
	assert(num_voxels > 0);
	// conPrint("Adding " + toString(num_voxels) + " voxels.");

	// Make hash from voxel indices to voxel material
	const Vec3<int> empty_key(std::numeric_limits<int>::max());
	HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc> voxel_hash(/*empty key=*/empty_key, /*expected_num_items=*/num_voxels);

	Vec3<int> minpos( 1000000000);
	Vec3<int> maxpos(-1000000000);

	int max_mat_index = 0;
	for(int v = 0; v < (int)num_voxels; ++v)
	{
		max_mat_index = myMax(max_mat_index, voxel_group.voxels[v].mat_index);
		voxel_hash.insert(std::make_pair(voxel_group.voxels[v].pos, voxel_group.voxels[v].mat_index));

		minpos = minpos.min(voxel_group.voxels[v].pos);
		maxpos = maxpos.max(voxel_group.voxels[v].pos);
	}
	const size_t num_mats = (size_t)max_mat_index + 1;

	minpos_out = minpos;
	maxpos_out = maxpos;

	//-------------- Sort voxels by material --------------------
	std::vector<Voxel> voxels(num_voxels);
	Sort::serialCountingSortWithNumBuckets(/*in=*/voxel_group.voxels.data(), /*out=*/voxels.data(), voxel_group.voxels.size(), num_mats, GetMatIndex());

	return doMakeIndigoMeshForVoxelGroup(voxels, num_mats, voxel_hash);
}


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <utils/TestUtils.h>


void VoxelMeshBuilding::test()
{
	conPrint("VoxelMeshBuilding::test()");

	glare::TaskManager task_manager;


	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(10, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(20, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(30, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(40, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(50, 0, 1), 1));

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(50, 0, 1));
		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 6 * 6 * 2);
	}

	// Test a single voxel
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(0, 0, 0));
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(1, 0, 0));
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels (along y axis) with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 1, 0), 0));


		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(0, 1, 0));
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels (along z axis) with same material.  Greedy meshing should result in just 6 quad faces (12 tris)
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(0, 0, 1));
		testAssert(data->num_materials_referenced == 1);
		testAssert(data->triangles.size() == 6 * 2);
	}

	// Test two adjacent voxels with different materials.  All faces should be added.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 1));

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		testAssert(minpos == Vec3<int>(0, 0, 0));
		testAssert(maxpos == Vec3<int>(0, 0, 1));
		testAssert(data->num_materials_referenced == 2);
		testAssert(data->triangles.size() == 2 * 6 * 2);
	}

	// Performance test
	if(true)
	{
		VoxelGroup group;
		for(int z=0; z<100; z += 2)
			for(int y=0; y<100; ++y)
				for(int x=0; x<10; ++x)
					group.voxels.push_back(Voxel(Vec3<int>(x, y, z), 0));


		Timer timer;

		Vec3<int> minpos, maxpos;
		Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, minpos, maxpos);

		conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
		conPrint("Resulting num tris: " + toString(data->triangles.size()));
	}

	conPrint("VoxelMeshBuilding::test() done.");
}


#endif // BUILD_TESTS
