/*=====================================================================
VoxelMeshBuilding.cpp
---------------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#include "VoxelMeshBuilding.h"


#include "../shared/WorldObject.h"
//#include "../shared/ResourceManager.h"
//#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
//#include "../graphics/formatdecoderobj.h"
//#include "../graphics/FormatDecoderSTL.h"
//#include "../graphics/FormatDecoderGLTF.h"
//#include "../graphics/FormatDecoderVox.h"
//#include "../simpleraytracer/raymesh.h"
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
#include <limits>


/*
For each voxel
	For each face
		if the face is not already marked as done, and if there is no adjacent voxel over the face:
			mark face as done
*/

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
	size_t operator() (const Vec3f& v) const
	{
		return hashBytes((const uint8*)&v.x, sizeof(Vec3f)); // TODO: use better hash func.
	}
};

struct VoxelsMatPred
{
	bool operator() (const Voxel& a, const Voxel& b)
	{
		return a.mat_index < b.mat_index;
	}
};

struct VoxelBuildInfo
{
	int face_offset; // number of faces added before this voxel.
	int num_faces; // num faces added for this voxel.
};


inline static int addVert(const Vec4f& vert_pos, const Vec2f& uv, HashMapInsertOnly2<Vec3f, int, Vec3fHashFunc>& vertpos_hash, float* const combined_data, int NUM_COMPONENTS)
{
	auto insert_res = vertpos_hash.insert(std::make_pair(Vec3f(vert_pos[0], vert_pos[1], vert_pos[2]), (int)vertpos_hash.size()));
	const int vertpos_i = insert_res.first->second; // deref iterator to get (vec3f, index) pair, then get the index.
	
	combined_data[vertpos_i * NUM_COMPONENTS + 0] = vert_pos[0];
	combined_data[vertpos_i * NUM_COMPONENTS + 1] = vert_pos[1];
	combined_data[vertpos_i * NUM_COMPONENTS + 2] = vert_pos[2];
	combined_data[vertpos_i * NUM_COMPONENTS + 3] = uv.x;
	combined_data[vertpos_i * NUM_COMPONENTS + 4] = uv.y;

	return vertpos_i;
}


struct GetMatIndex
{
	size_t operator() (const Voxel& v)
	{
		return (size_t)v.mat_index;
	}
};


static const int NUM_COMPONENTS = 5; // num float components per vertex.


#if 0
static Reference<OpenGLMeshRenderData> makeMeshForVoxelGroup(const std::vector<Voxel>& voxels, const size_t num_mats, const HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc>& voxel_hash)
{
	const size_t num_voxels = voxels.size();

	Reference<OpenGLMeshRenderData> meshdata = new OpenGLMeshRenderData();
	meshdata->has_uvs = true;
	meshdata->has_shading_normals = false;
	meshdata->batches.reserve(num_mats);

	const Vec3f vertpos_empty_key(std::numeric_limits<float>::max());
	HashMapInsertOnly2<Vec3f, int, Vec3fHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/num_voxels);

	const size_t num_faces_upper_bound = num_voxels * 6;

	const float w = 1.f; // voxel width

	
	meshdata->vert_data.resizeNoCopy(num_faces_upper_bound*4 * NUM_COMPONENTS * sizeof(float)); // num verts = num_faces*4
	float* combined_data = (float*)meshdata->vert_data.data();


	js::Vector<uint32, 16>& mesh_indices = meshdata->vert_index_buffer;
	mesh_indices.resizeNoCopy(num_faces_upper_bound * 6);

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	size_t face = 0; // total face write index

	int prev_mat_i = -1;
	size_t prev_start_face_i = 0;

	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int voxel_mat_i = voxels[v].mat_index;

		if(voxel_mat_i != prev_mat_i)
		{
			// Create a new batch
			if(face > prev_start_face_i)
			{
				meshdata->batches.push_back(OpenGLBatch());
				meshdata->batches.back().material_index = prev_mat_i;
				meshdata->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;
				meshdata->batches.back().prim_start_offset = (uint32)(prev_start_face_i * 6 * sizeof(uint32)); // Offset in bytes

				prev_start_face_i = face;
			}
		}
		prev_mat_i = voxel_mat_i;

		const Vec3<int> v_p = voxels[v].pos;
		const Vec4f v_pf((float)v_p.x, (float)v_p.y, (float)v_p.z, 0); // voxel_pos_offset

		// We will nudge the vertices outwards along the face normal a little.
		// This means that vertices from non-coplanar faces that share the same position, and which shouldn't get merged due to differing uvs, won't.
		// Note that we could also achieve this by using the UV in the hash table key.
		const float nudge = 1.0e-4f;

		// x = 0 face
		auto res = voxel_hash.find(Vec3<int>(v_p.x - 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(-nudge, 0, 0, 1) + v_pf, Vec2f(1 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(-nudge, 0, w, 1) + v_pf, Vec2f(1 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(-nudge, w, w, 1) + v_pf, Vec2f(0 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(-nudge, w, 0, 1) + v_pf, Vec2f(0 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// x = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x + 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(w + nudge, 0, 0, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w + nudge, w, 0, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w + nudge, w, w, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w + nudge, 0, w, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y - 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0 - nudge, 0, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0 - nudge, 0, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, 0 - nudge, w, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, 0 - nudge, w, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y + 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, w + nudge, 0, 1) + v_pf, Vec2f(1 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w + nudge, w, 1) + v_pf, Vec2f(1 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w + nudge, w, 1) + v_pf, Vec2f(0 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, w + nudge, 0, 1) + v_pf, Vec2f(0 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z - 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z + 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, w, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);


			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		aabb_os.enlargeToHoldPoint(v_pf);
	}

	// Add last batch
	if(face > prev_start_face_i)
	{
		meshdata->batches.push_back(OpenGLBatch());
		meshdata->batches.back().material_index = prev_mat_i;
		meshdata->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;
		meshdata->batches.back().prim_start_offset = (uint32)(prev_start_face_i * 6 * sizeof(uint32)); // Offset in bytes
	}

	meshdata->aabb_os = js::AABBox(aabb_os.min_, aabb_os.max_ + Vec4f(w, w, w, 0)); // Extend AABB to enclose the +xyz bounds of the voxels.

	const size_t num_faces = face;
	const size_t num_verts = vertpos_hash.size();

	// Trim arrays to actual size
	meshdata->vert_data.resize(num_verts * NUM_COMPONENTS * sizeof(float));
	meshdata->vert_index_buffer.resize(num_faces * 6);

	return meshdata;
}
#endif


static Reference<BatchedMesh> doMakeBatchedMeshForVoxelGroup(const std::vector<Voxel>& voxels, const size_t num_mats, const HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc>& voxel_hash)
{
	const size_t num_voxels = voxels.size();

	Reference<BatchedMesh> mesh = new BatchedMesh();

	mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Position, BatchedMesh::ComponentType_Float, /*offset_B=*/0));
	mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0, BatchedMesh::ComponentType_Float, /*offset_B=*/3 * sizeof(float)));

	mesh->batches.reserve(num_mats);

	const Vec3f vertpos_empty_key(std::numeric_limits<float>::max());
	HashMapInsertOnly2<Vec3f, int, Vec3fHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/num_voxels);

	const size_t num_faces_upper_bound = num_voxels * 6;

	const float w = 1.f; // voxel width


	mesh->vertex_data.resizeNoCopy(num_faces_upper_bound*4 * NUM_COMPONENTS * sizeof(float)); // num verts = num_faces*4
	float* combined_data = (float*)mesh->vertex_data.data();


	//js::Vector<uint8, 16>& mesh_indices = mesh->index_data;
	//mesh_indices.resizeNoCopy(num_faces_upper_bound * 6);

	js::Vector<uint32, 16> mesh_indices;
	mesh_indices.resizeNoCopy(num_faces_upper_bound * 6);

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	size_t face = 0; // total face write index

	int prev_mat_i = -1;
	size_t prev_start_face_i = 0;

	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int voxel_mat_i = voxels[v].mat_index;

		if(voxel_mat_i != prev_mat_i)
		{
			// Create a new batch
			if(face > prev_start_face_i)
			{
				mesh->batches.push_back(BatchedMesh::IndicesBatch());
				mesh->batches.back().indices_start = (uint32)(prev_start_face_i * 6);
				mesh->batches.back().material_index = prev_mat_i;
				mesh->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;

				prev_start_face_i = face;
			}
		}
		prev_mat_i = voxel_mat_i;

		const Vec3<int> v_p = voxels[v].pos;
		const Vec4f v_pf((float)v_p.x, (float)v_p.y, (float)v_p.z, 0); // voxel_pos_offset

		// We will nudge the vertices outwards along the face normal a little.
		// This means that vertices from non-coplanar faces that share the same position, and which shouldn't get merged due to differing uvs, won't.
		// Note that we could also achieve this by using the UV in the hash table key.
		const float nudge = 1.0e-4f;

		// x = 0 face
		auto res = voxel_hash.find(Vec3<int>(v_p.x - 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(-nudge, 0, 0, 1) + v_pf, Vec2f(1 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(-nudge, 0, w, 1) + v_pf, Vec2f(1 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(-nudge, w, w, 1) + v_pf, Vec2f(0 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(-nudge, w, 0, 1) + v_pf, Vec2f(0 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// x = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x + 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(w + nudge, 0, 0, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w + nudge, w, 0, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w + nudge, w, w, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w + nudge, 0, w, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y - 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0 - nudge, 0, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0 - nudge, 0, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, 0 - nudge, w, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, 0 - nudge, w, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y + 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, w + nudge, 0, 1) + v_pf, Vec2f(1 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w + nudge, w, 1) + v_pf, Vec2f(1 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w + nudge, w, 1) + v_pf, Vec2f(0 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, w + nudge, 0, 1) + v_pf, Vec2f(0 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z - 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z + 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, w, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);


			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		aabb_os.enlargeToHoldPoint(v_pf);
	}

	// Add last batch
	if(face > prev_start_face_i)
	{
		mesh->batches.push_back(BatchedMesh::IndicesBatch());
		mesh->batches.back().indices_start = (uint32)(prev_start_face_i * 6);
		mesh->batches.back().material_index = prev_mat_i;
		mesh->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;
	}

	mesh->aabb_os = js::AABBox(aabb_os.min_, aabb_os.max_ + Vec4f(w, w, w, 0)); // Extend AABB to enclose the +xyz bounds of the voxels.

	const size_t num_faces = face;
	const size_t num_verts = vertpos_hash.size();
	const size_t num_indices = num_faces * 6;

	// Trim arrays to actual size
	mesh->vertex_data.resize(num_verts * NUM_COMPONENTS * sizeof(float));
	
	
	
	// Copy mesh indices
	//TEMP: just use uint32 indices.
	mesh->index_type = BatchedMesh::ComponentType_UInt32;
	mesh->index_data.resize(num_indices * sizeof(uint32));
	std::memcpy(mesh->index_data.data(), mesh_indices.data(), mesh->index_data.size());
	
	return mesh;
}


Reference<BatchedMesh> VoxelMeshBuilding::makeBatchedMeshForVoxelGroup(const VoxelGroup& voxel_group)
{
	const size_t num_voxels = voxel_group.voxels.size();
	assert(num_voxels > 0);
	// conPrint("Adding " + toString(num_voxels) + " voxels.");

	// Make hash from voxel indices to voxel material
	const Vec3<int> empty_key(std::numeric_limits<int>::max());
	HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc> voxel_hash(/*empty key=*/empty_key, /*expected_num_items=*/num_voxels);

	int max_mat_index = 0;
	for(int v=0; v<(int)num_voxels; ++v)
	{
		max_mat_index = myMax(max_mat_index, voxel_group.voxels[v].mat_index);
		voxel_hash.insert(std::make_pair(voxel_group.voxels[v].pos, voxel_group.voxels[v].mat_index));
	}
	const size_t num_mats = (size_t)max_mat_index + 1;

	//-------------- Sort voxels by material --------------------
	std::vector<Voxel> voxels(num_voxels);
	Sort::serialCountingSortWithNumBuckets(/*in=*/voxel_group.voxels.data(), /*out=*/voxels.data(), voxel_group.voxels.size(), num_mats, GetMatIndex());

	return doMakeBatchedMeshForVoxelGroup(voxels, num_mats, voxel_hash);
}


#if 0
Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out)
{
	const size_t num_voxels = voxel_group.voxels.size();
	assert(num_voxels > 0);
	// conPrint("Adding " + toString(num_voxels) + " voxels.");

	// Make hash from voxel indices to voxel material
	const Vec3<int> empty_key(std::numeric_limits<int>::max());
	HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc> voxel_hash(/*empty key=*/empty_key, /*expected_num_items=*/num_voxels);

	int max_mat_index = 0;
	for(int v=0; v<(int)num_voxels; ++v)
	{
		max_mat_index = myMax(max_mat_index, voxel_group.voxels[v].mat_index);
		voxel_hash.insert(std::make_pair(voxel_group.voxels[v].pos, voxel_group.voxels[v].mat_index));
	}
	const size_t num_mats = (size_t)max_mat_index + 1;

	//-------------- Sort voxels by material --------------------
	std::vector<Voxel> voxels(num_voxels);
	Sort::serialCountingSortWithNumBuckets(/*in=*/voxel_group.voxels.data(), /*out=*/voxels.data(), voxel_group.voxels.size(), num_mats, GetMatIndex());

	Reference<OpenGLMeshRenderData> meshdata = makeMeshForVoxelGroup(voxels, num_mats, voxel_hash);

#if 0
	Reference<OpenGLMeshRenderData> meshdata = new OpenGLMeshRenderData();
	meshdata->has_uvs = true;
	meshdata->has_shading_normals = false;
	meshdata->batches.reserve(num_mats);

	const float w = 1.f; // voxel width

	const Vec3f vertpos_empty_key(std::numeric_limits<float>::max());
	HashMapInsertOnly2<Vec3f, int, Vec3fHashFunc> vertpos_hash(/*empty key=*/vertpos_empty_key, /*expected_num_items=*/num_voxels);


	const size_t num_faces_upper_bound = num_voxels * 6;
	

	const int NUM_COMPONENTS = 5; // num float components per vertex.
	meshdata->vert_data.resizeNoCopy(num_faces_upper_bound*4 * NUM_COMPONENTS * sizeof(float)); // num verts = num_faces*4
	float* combined_data = (float*)meshdata->vert_data.data();


	js::Vector<uint32, 16>& mesh_indices = meshdata->vert_index_buffer;
	mesh_indices.resizeNoCopy(num_faces_upper_bound * 6);

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	size_t face = 0; // total face write index

	int prev_mat_i = -1;
	size_t prev_start_face_i = 0;

	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int voxel_mat_i = voxels[v].mat_index;

		if(voxel_mat_i != prev_mat_i)
		{
			// Create a new batch
			if(face > prev_start_face_i)
			{
				meshdata->batches.push_back(OpenGLBatch());
				meshdata->batches.back().material_index = prev_mat_i;
				meshdata->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;
				meshdata->batches.back().prim_start_offset = (uint32)(prev_start_face_i * 6 * sizeof(uint32)); // Offset in bytes

				prev_start_face_i = face;
			}
		}
		prev_mat_i = voxel_mat_i;

		const Vec3<int> v_p = voxels[v].pos;
		const Vec4f v_pf((float)v_p.x, (float)v_p.y, (float)v_p.z, 0); // voxel_pos_offset

		// We will nudge the vertices outwards along the face normal a little.
		// This means that vertices from non-coplanar faces that share the same position, and which shouldn't get merged due to differing uvs, won't.
		// Note that we could also achieve this by using the UV in the hash table key.
		const float nudge = 1.0e-4f;

		// x = 0 face
		auto res = voxel_hash.find(Vec3<int>(v_p.x - 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(-nudge, 0, 0, 1) + v_pf, Vec2f(1 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(-nudge, 0, w, 1) + v_pf, Vec2f(1 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(-nudge, w, w, 1) + v_pf, Vec2f(0 - v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(-nudge, w, 0, 1) + v_pf, Vec2f(0 - v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// x = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x + 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(w + nudge, 0, 0, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w + nudge, w, 0, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w + nudge, w, w, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w + nudge, 0, w, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y - 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0 - nudge, 0, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0 - nudge, 0, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, 0 - nudge, w, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, 0 - nudge, w, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// y = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y + 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, w + nudge, 0, 1) + v_pf, Vec2f(1 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w + nudge, w, 1) + v_pf, Vec2f(1 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w + nudge, w, 1) + v_pf, Vec2f(0 - v_pf[0], 1 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, w + nudge, 0, 1) + v_pf, Vec2f(0 - v_pf[0], 0 + v_pf[2]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z - 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(0, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 0 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, 0 - nudge, 1) + v_pf, Vec2f(1 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(w, 0, 0 - nudge, 1) + v_pf, Vec2f(0 + v_pf[1], 1 + v_pf[0]), vertpos_hash, combined_data, NUM_COMPONENTS);

			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		// z = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z + 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			const int v0i = addVert(Vec4f(0, 0, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v1i = addVert(Vec4f(w, 0, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 0 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v2i = addVert(Vec4f(w, w, w + nudge, 1) + v_pf, Vec2f(1 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);
			const int v3i = addVert(Vec4f(0, w, w + nudge, 1) + v_pf, Vec2f(0 + v_pf[0], 1 + v_pf[1]), vertpos_hash, combined_data, NUM_COMPONENTS);


			mesh_indices[face * 6 + 0] = v0i;
			mesh_indices[face * 6 + 1] = v1i;
			mesh_indices[face * 6 + 2] = v2i;
			mesh_indices[face * 6 + 3] = v0i;
			mesh_indices[face * 6 + 4] = v2i;
			mesh_indices[face * 6 + 5] = v3i;
			face++;
		}

		aabb_os.enlargeToHoldPoint(v_pf);
	}

	// Add last batch
	if(face > prev_start_face_i)
	{
		meshdata->batches.push_back(OpenGLBatch());
		meshdata->batches.back().material_index = prev_mat_i;
		meshdata->batches.back().num_indices = (uint32)(face - prev_start_face_i) * 6;
		meshdata->batches.back().prim_start_offset = (uint32)(prev_start_face_i * 6 * sizeof(uint32)); // Offset in bytes
	}

	meshdata->aabb_os = js::AABBox(aabb_os.min_, aabb_os.max_ + Vec4f(w, w, w, 0)); // Extend AABB to enclose the +xyz bounds of the voxels.

	const size_t num_faces = face;
	const size_t num_verts = vertpos_hash.size();

	// Trim arrays to actual size
	meshdata->vert_data.resize(num_verts * NUM_COMPONENTS * sizeof(float));
	meshdata->vert_index_buffer.resize(num_faces * 6);
#endif

	


	//------------------------------------------------------------------------------------
	Reference<RayMesh> raymesh = new RayMesh("mesh", /*enable shading normals=*/false);

	// Copy over tris to raymesh
	const size_t num_verts = meshdata->vert_data.size() / (NUM_COMPONENTS * sizeof(float));
	const size_t num_faces = meshdata->vert_index_buffer.size() / 6;
	const js::Vector<uint32, 16>& mesh_indices = meshdata->vert_index_buffer;
	const float* combined_data = (float*)meshdata->vert_data.data();
	 
	raymesh->getTriangles().resizeNoCopy(num_faces * 2);
	RayMeshTriangle* const dest_tris = raymesh->getTriangles().data();

	for(size_t b=0; b<meshdata->batches.size(); ++b) // Iterate over batches to do this so we know the material index for each face.
	{
		const int batch_num_faces = meshdata->batches[b].num_indices / 6;
		const int start_face      = meshdata->batches[b].prim_start_offset / (6 * sizeof(uint32));
		const int mat_index       = meshdata->batches[b].material_index;

		for(size_t f=start_face; f<start_face + batch_num_faces; ++f)
		{
			dest_tris[f*2 + 0].vertex_indices[0] = mesh_indices[f * 6 + 0];
			dest_tris[f*2 + 0].vertex_indices[1] = mesh_indices[f * 6 + 1];
			dest_tris[f*2 + 0].vertex_indices[2] = mesh_indices[f * 6 + 2];
			dest_tris[f*2 + 0].uv_indices[0] = 0;
			dest_tris[f*2 + 0].uv_indices[1] = 0;
			dest_tris[f*2 + 0].uv_indices[2] = 0;
			dest_tris[f*2 + 0].setMatIndexAndUseShadingNormals(mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);

			dest_tris[f*2 + 1].vertex_indices[0] =  mesh_indices[f * 6 + 3];
			dest_tris[f*2 + 1].vertex_indices[1] =  mesh_indices[f * 6 + 4];
			dest_tris[f*2 + 1].vertex_indices[2] =  mesh_indices[f * 6 + 5];
			dest_tris[f*2 + 1].uv_indices[0] = 0;
			dest_tris[f*2 + 1].uv_indices[1] = 0;
			dest_tris[f*2 + 1].uv_indices[2] = 0;
			dest_tris[f*2 + 1].setMatIndexAndUseShadingNormals(mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);
		}
	}
	
	// Copy verts positions and normals
	raymesh->getVertices().resizeNoCopy(num_verts);
	RayMeshVertex* const dest_verts = raymesh->getVertices().data();
	for(size_t i=0; i<num_verts; ++i)
	{
		dest_verts[i].pos.x = combined_data[i * NUM_COMPONENTS + 0];
		dest_verts[i].pos.y = combined_data[i * NUM_COMPONENTS + 1];
		dest_verts[i].pos.z = combined_data[i * NUM_COMPONENTS + 2];

		// Skip UV data for now

		dest_verts[i].normal = Vec3f(1, 0, 0);
	}

	// Set UVs (Note: only needed for dumping RayMesh to disk)
	raymesh->setMaxNumTexcoordSets(0);
	//raymesh->getUVs().resize(4);
	//raymesh->getUVs()[0] = Vec2f(0, 0);
	//raymesh->getUVs()[1] = Vec2f(0, 1);
	//raymesh->getUVs()[2] = Vec2f(1, 1);
	//raymesh->getUVs()[3] = Vec2f(1, 0);

	Geometry::BuildOptions options;
	DummyShouldCancelCallback should_cancel_callback;
	StandardPrintOutput print_output;
	raymesh->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);
	raymesh_out = raymesh;
	//--------------------------------------------------------------------------------------

	const size_t vert_index_buffer_size = meshdata->vert_index_buffer.size();
	if(num_verts < 256)
	{
		js::Vector<uint8, 16>& index_buf = meshdata->vert_index_buffer_uint8;
		index_buf.resize(vert_index_buffer_size);
		for(size_t i=0; i<vert_index_buffer_size; ++i)
		{
			assert(mesh_indices[i] < 256);
			index_buf[i] = (uint8)mesh_indices[i];
		}
		if(do_opengl_stuff)
			meshdata->vert_indices_buf = new VBO(index_buf.data(), index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);

		meshdata->index_type = GL_UNSIGNED_BYTE;
		// Go through the batches and adjust the start offset to take into account we're using uint8s.
		for(size_t i=0; i<meshdata->batches.size(); ++i)
			meshdata->batches[i].prim_start_offset /= 4;
	}
	else if(num_verts < 65536)
	{
		js::Vector<uint16, 16>& index_buf = meshdata->vert_index_buffer_uint16;
		index_buf.resize(vert_index_buffer_size);
		for(size_t i=0; i<vert_index_buffer_size; ++i)
		{
			assert(mesh_indices[i] < 65536);
			index_buf[i] = (uint16)mesh_indices[i];
		}
		if(do_opengl_stuff)
			meshdata->vert_indices_buf = new VBO(index_buf.data(), index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);

		meshdata->index_type = GL_UNSIGNED_SHORT;
		// Go through the batches and adjust the start offset to take into account we're using uint16s.
		for(size_t i=0; i<meshdata->batches.size(); ++i)
			meshdata->batches[i].prim_start_offset /= 2;
	}
	else
	{
		if(do_opengl_stuff)
			meshdata->vert_indices_buf = new VBO(mesh_indices.data(), mesh_indices.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
		meshdata->index_type = GL_UNSIGNED_INT;
	}


	if(do_opengl_stuff)
		meshdata->vert_vbo = new VBO(meshdata->vert_data.data(), meshdata->vert_data.dataSizeBytes());

	VertexSpec& spec = meshdata->vertex_spec;
	const uint32 vert_stride = (uint32)(sizeof(float) * 3 + sizeof(float) * 2); // also vertex size.

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = vert_stride;
	pos_attrib.offset = 0;
	spec.attributes.push_back(pos_attrib);

	VertexAttrib normal_attrib; // NOTE: We need this attribute, disabled, because it's expected, see OpenGLProgram.cpp
	normal_attrib.enabled = false;
	normal_attrib.num_comps = 3;
	normal_attrib.type = GL_FLOAT;
	normal_attrib.normalised = false;
	normal_attrib.stride = vert_stride;
	normal_attrib.offset = sizeof(float) * 3; // goes after position
	spec.attributes.push_back(normal_attrib);

	VertexAttrib uv_attrib;
	uv_attrib.enabled = true;
	uv_attrib.num_comps = 2;
	uv_attrib.type = GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = vert_stride;
	uv_attrib.offset = (uint32)(sizeof(float) * 3); // after position
	spec.attributes.push_back(uv_attrib);

	if(do_opengl_stuff)
		meshdata->vert_vao = new VAO(meshdata->vert_vbo, spec);

	// If we did the OpenGL calls, then the data has been uploaded to VBOs etc.. so we can free it.
	if(do_opengl_stuff)
	{
		meshdata->vert_data.clearAndFreeMem();
		meshdata->vert_index_buffer.clearAndFreeMem();
		meshdata->vert_index_buffer_uint16.clearAndFreeMem();
		meshdata->vert_index_buffer_uint8.clearAndFreeMem();
	}

	return meshdata;
}
#endif


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <indigo/TestUtils.h>


void VoxelMeshBuilding::test()
{
	Indigo::TaskManager task_manager;

#if 0
	// Test two adjacent voxels with different materials.  All faces should be added.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(10, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(20, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(30, 0, 1), 1));
		group.voxels.push_back(Voxel(Vec3<int>(40, 0, 1), 0));
		group.voxels.push_back(Voxel(Vec3<int>(50, 0, 1), 1));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(data->batches.size() == 2);
		testAssert(raymesh->getTriangles().size() == 6 * 6 * 2);
	}


	
	// Test a single voxel
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(raymesh->getTriangles().size() == 6 * 2);
	}

	// Test two adjacent voxels with same material.  Two cube faces on each voxel should be missing.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(raymesh->getTriangles().size() == 2 * 5 * 2);
	}

	// Test two adjacent voxels (along y axis) with same material.  Two cube faces on each voxel should be missing.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 1, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(raymesh->getTriangles().size() == 2 * 5 * 2);
	}

	// Test two adjacent voxels (along z axis) with same material.  Two cube faces on each voxel should be missing.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(raymesh->getTriangles().size() == 2 * 5 * 2);
	}

	// Test two adjacent voxels with different materials.  All faces should be added.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 1));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testAssert(raymesh->getTriangles().size() == 2 * 6 * 2);
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
		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
		conPrint("Resulting num tris: " + toString(raymesh->getTriangles().size()));
	}
#endif
}


#endif
