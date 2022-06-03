/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "MeshBuilding.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
#include "../graphics/formatdecoderobj.h"
#include "../graphics/FormatDecoderSTL.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../graphics/FormatDecoderVox.h"
#include "../simpleraytracer/raymesh.h"
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
#include "../utils/IncludeHalf.h"
#include "../opengl/GLMeshBuilding.h"
#include "../opengl/IncludeOpenGL.h"
#include "../indigo/UVUnwrapper.h"
#include <limits>


void ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.tex_path = mat.colour_texture_url;

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.metallic_roughness_tex_path = mat.roughness.texture_url;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	// glTexImage2D expects the start of the texture data to be the lower left of the image, whereas it is actually the upper left.  So flip y coord to compensate.
	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;
}


static const std::string toLocalPath(const std::string& URL, ResourceManager& resource_manager)
{
	if(URL.empty())
		return "";
	else
	{
		const bool streamable = ::hasExtensionStringView(URL, "mp4");
		if(streamable)
			return URL; // Just leave streamable URLs as-is.
		else
			return resource_manager.pathForURL(URL);
	}
}


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, int lod_level, const std::string& lightmap_url, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	if(!mat.colour_texture_url.empty())
		opengl_mat.tex_path = toLocalPath(mat.getLODTextureURLForLevel(mat.colour_texture_url, lod_level, /*has alpha=*/mat.colourTexHasAlpha()), resource_manager);
	else
		opengl_mat.tex_path.clear();

	if(!mat.roughness.texture_url.empty())
		opengl_mat.metallic_roughness_tex_path = toLocalPath(mat.getLODTextureURLForLevel(mat.roughness.texture_url, lod_level, /*has alpha=*/false), resource_manager);
	else
		opengl_mat.metallic_roughness_tex_path.clear();

	if(!lightmap_url.empty())
		opengl_mat.lightmap_path = toLocalPath(WorldObject::getLODLightmapURL(lightmap_url, lod_level), resource_manager);
	else
		opengl_mat.lightmap_path.clear();

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	// glTexImage2D expects the start of the texture data to be the lower left of the image, whereas it is actually the upper left.  So flip y coord to compensate.
	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;

	if(::hasExtensionStringView(opengl_mat.tex_path, "mp4"))
		opengl_mat.convert_albedo_from_srgb = true;
}


void ModelLoading::checkValidAndSanitiseMesh(Indigo::Mesh& mesh)
{
	if(mesh.num_uv_mappings > 10)
		throw glare::Exception("Too many UV sets: " + toString(mesh.num_uv_mappings) + ", max is " + toString(10));

/*	if(mesh.vert_normals.size() == 0)
	{
		for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
		{
			this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
			this->vertices[i].normal.set(0.f, 0.f, 0.f);
		}

		vertex_shading_normals_provided = false;
	}
	else
	{
		assert(mesh.vert_normals.size() == mesh.vert_positions.size());

		for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
		{
			this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
			this->vertices[i].normal.set(mesh.vert_normals[i].x, mesh.vert_normals[i].y, mesh.vert_normals[i].z);

			assert(::isFinite(mesh.vert_normals[i].x) && ::isFinite(mesh.vert_normals[i].y) && ::isFinite(mesh.vert_normals[i].z));
		}

		vertex_shading_normals_provided = true;
	}*/


	// Check any supplied normals are valid.
	for(size_t i=0; i<mesh.vert_normals.size(); ++i)
	{
		const float len2 = mesh.vert_normals[i].length2();
		if(!::isFinite(len2))
			mesh.vert_normals[i] = Indigo::Vec3f(1, 0, 0);
		else
		{
			// NOTE: allow non-unit normals?
		}
	}

	// Copy UVs from Indigo::Mesh
	assert(mesh.num_uv_mappings == 0 || (mesh.uv_pairs.size() % mesh.num_uv_mappings == 0));

	// Check all UVs are not NaNs, as NaN UVs cause NaN filtered texture values, which cause a crash in TextureUnit table look-up.  See https://bugs.glaretechnologies.com/issues/271
	const size_t uv_size = mesh.uv_pairs.size();
	for(size_t i=0; i<uv_size; ++i)
	{
		if(!isFinite(mesh.uv_pairs[i].x))
			mesh.uv_pairs[i].x = 0;
		if(!isFinite(mesh.uv_pairs[i].y))
			mesh.uv_pairs[i].y = 0;
	}

	const uint32 num_uv_groups = (mesh.num_uv_mappings == 0) ? 0 : ((uint32)mesh.uv_pairs.size() / mesh.num_uv_mappings);
	const uint32 num_verts = (uint32)mesh.vert_positions.size();

	// Tris
	for(size_t i = 0; i < mesh.triangles.size(); ++i)
	{
		const Indigo::Triangle& src_tri = mesh.triangles[i];

		// Check vertex indices are in bounds
		for(unsigned int v = 0; v < 3; ++v)
			if(src_tri.vertex_indices[v] >= num_verts)
				throw glare::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(mesh.triangles[i].vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 3; ++v)
				if(src_tri.uv_indices[v] >= num_uv_groups)
					throw glare::Exception("Triangle uv index is out of bounds.  (uv index=" + toString(mesh.triangles[i].uv_indices[v]) + ")");
	}

	// Quads
	for(size_t i = 0; i < mesh.quads.size(); ++i)
	{
		// Check vertex indices are in bounds
		for(unsigned int v = 0; v < 4; ++v)
			if(mesh.quads[i].vertex_indices[v] >= num_verts)
				throw glare::Exception("Quad vertex index is out of bounds.  (vertex index=" + toString(mesh.quads[i].vertex_indices[v]) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 4; ++v)
				if(mesh.quads[i].uv_indices[v] >= num_uv_groups)
					throw glare::Exception("Quad uv index is out of bounds.  (uv index=" + toString(mesh.quads[i].uv_indices[v]) + ")");
	}
}


void ModelLoading::checkValidAndSanitiseMesh(BatchedMesh& mesh)
{
	if(mesh.numMaterialsReferenced() > 10000)
		throw glare::Exception("Too many materials referenced.");

	/*	if(mesh.vert_normals.size() == 0)
		{
			for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
			{
				this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
				this->vertices[i].normal.set(0.f, 0.f, 0.f);
			}

			vertex_shading_normals_provided = false;
		}
		else
		{
			assert(mesh.vert_normals.size() == mesh.vert_positions.size());

			for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
			{
				this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
				this->vertices[i].normal.set(mesh.vert_normals[i].x, mesh.vert_normals[i].y, mesh.vert_normals[i].z);

				assert(::isFinite(mesh.vert_normals[i].x) && ::isFinite(mesh.vert_normals[i].y) && ::isFinite(mesh.vert_normals[i].z));
			}

			vertex_shading_normals_provided = true;
		}*/


	// Check any supplied normals are valid.
	// NOTE: since all batched meshes currently use packed normal encoding, we can skip this
	
	// Check all UVs are not NaNs, as NaN UVs cause NaN filtered texture values, which cause a crash in TextureUnit table look-up.  See https://bugs.glaretechnologies.com/issues/271
	//const size_t uv_size = mesh.uv_pairs.size();
	//for(size_t i=0; i<uv_size; ++i)
	//{
	//	if(!isFinite(mesh.uv_pairs[i].x))
	//		mesh.uv_pairs[i].x = 0;
	//	if(!isFinite(mesh.uv_pairs[i].y))
	//		mesh.uv_pairs[i].y = 0;
	//}

	const uint32 num_verts = (uint32)mesh.numVerts();


	const BatchedMesh::ComponentType index_type = mesh.index_type;
	const size_t num_indices = mesh.numIndices();
	const size_t num_tris = num_indices / 3;

	const uint8* const index_data_uint8  = (const uint8*)mesh.index_data.data();
	const uint16* const index_data_uint16 = (const uint16*)mesh.index_data.data();
	const uint32* const index_data_uint32 = (const uint32*)mesh.index_data.data();

	for(size_t t = 0; t < num_tris; ++t)
	{
		uint32 vertex_indices[3];
		if(index_type == BatchedMesh::ComponentType_UInt8)
		{
			vertex_indices[0] = index_data_uint8[t*3 + 0];
			vertex_indices[1] = index_data_uint8[t*3 + 1];
			vertex_indices[2] = index_data_uint8[t*3 + 2];
		}
		else if(index_type == BatchedMesh::ComponentType_UInt16)
		{
			vertex_indices[0] = index_data_uint16[t*3 + 0];
			vertex_indices[1] = index_data_uint16[t*3 + 1];
			vertex_indices[2] = index_data_uint16[t*3 + 2];
		}
		else if(index_type == BatchedMesh::ComponentType_UInt32)
		{
			vertex_indices[0] = index_data_uint32[t*3 + 0];
			vertex_indices[1] = index_data_uint32[t*3 + 1];
			vertex_indices[2] = index_data_uint32[t*3 + 2];
		}
		else
			throw glare::Exception("Invalid index_type.");

		for(unsigned int v = 0; v < 3; ++v)
			if(vertex_indices[v] >= num_verts)
				throw glare::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");
	}

	js::Vector<uint8, 16>& vert_data = mesh.vertex_data;
	const size_t vert_size_B = mesh.vertexSize();
	const size_t num_joints = mesh.animation_data.joint_nodes.size();

	// Check joint indices are valid for all vertices
	const BatchedMesh::VertAttribute* joints_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Joints);
	if(joints_attr)
	{
		const size_t joint_offset_B = joints_attr->offset_B;
		if(joints_attr->component_type == BatchedMesh::ComponentType_UInt8)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint8 joints[4];
				std::memcpy(joints, &vert_data[i * vert_size_B + joint_offset_B], sizeof(uint8) * 4);

				for(int c=0; c<4; ++c)
					if((size_t)joints[c] >= num_joints)
						throw glare::Exception("Joint index is out of bounds");
			}
		}
		else if(joints_attr->component_type == BatchedMesh::ComponentType_UInt16)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint16 joints[4];
				std::memcpy(joints, &vert_data[i * vert_size_B + joint_offset_B], sizeof(uint16) * 4);

				for(int c=0; c<4; ++c)
					if((size_t)joints[c] >= num_joints)
						throw glare::Exception("Joint index is out of bounds");
			}
		}
		else
			throw glare::Exception("Invalid joint index component type: " + toString((uint32)joints_attr->component_type));
	}

	// Check weight data is valid for all vertices.
	// For now we will just catch cases where all weights are zero, and set one of them to 1 (or the uint equiv).
	// We could also normalise the weight sum but that would be slower.
	// This prevents the rendering error with dancedevil_glb_16934124793649044515_lod2.bmesh.
	const BatchedMesh::VertAttribute* weight_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Weights);
	if(weight_attr)
	{
		//conPrint("Checking weight data");
		//Timer timer;

		const size_t weights_offset_B = weight_attr->offset_B;
		if(weight_attr->component_type == BatchedMesh::ComponentType_UInt8)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint8 weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(uint8) * 4);
				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 255;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(uint8) * 4); // Copy back to vertex data
				}
			}
		}
		else if(weight_attr->component_type == BatchedMesh::ComponentType_UInt16)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint16 weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(uint16) * 4);
				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 65535;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(uint16) * 4); // Copy back to vertex data
				}
			}
		}
		else if(weight_attr->component_type == BatchedMesh::ComponentType_Float)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				float weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(float) * 4);
				/*const float sum = (weights[0] + weights[1]) + (weights[2] + weights[3]);
				if(sum < 0.9999f || sum >= 1.0001f)
				{
					if(std::fabs(sum) < 1.0e-3f)
					{
						weights[0] = 1.f;
						std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
					}
					else
					{
						const float scale = 1 / sum;
						for(int c=0; c<4; ++c) weights[c] *= scale;
						std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
					}
				}*/

				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 1.f;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
				}
			}
		}

		//conPrint("Checking weight data took " + timer.elapsedStringNSigFigs(4));
	}

}


static float getScaleForMesh(const BatchedMesh& mesh)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 5.0f;
	float use_scale = 1.f;
	const js::AABBox aabb = mesh.aabb_os;
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.1f;
			span *= 0.1f;
		}
	}


	// Scale up if needed
	const float min_span = 0.01f;
	if(::isFinite(span))
	{
		while(span <= min_span)
		{
			use_scale *= 10.f;
			span *= 10.f;
		}
	}

	return use_scale;
}


static void scaleMesh(Indigo::Mesh& mesh)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 5.0f;
	float use_scale = 1.f;
	const js::AABBox aabb(
		Vec4f(mesh.aabb_os.bound[0].x, mesh.aabb_os.bound[0].y, mesh.aabb_os.bound[0].z, 1.f),
		Vec4f(mesh.aabb_os.bound[1].x, mesh.aabb_os.bound[1].y, mesh.aabb_os.bound[1].z, 1.f));
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.1f;
			span *= 0.1f;
		}
	}


	// Scale up if needed
	const float min_span = 0.01f;
	if(::isFinite(span))
	{
		while(span <= min_span)
		{
			use_scale *= 10.f;
			span *= 10.f;
		}
	}


	if(use_scale != 1.f)
	{
		conPrint("Scaling object by " + toString(use_scale));
		for(size_t i=0; i<mesh.vert_positions.size(); ++i)
			mesh.vert_positions[i] *= use_scale;

		mesh.aabb_os.bound[0] *= use_scale;
		mesh.aabb_os.bound[1] *= use_scale;
	}
}


static float getScaleForVoxModel(const js::AABBox& aabb)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 2.0f;
	float use_scale = 1.f;
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.5f;
			span *= 0.5f;
		}
	}

	return use_scale;
}


// Rotate vertices around the y axis by half a turn, so that the figure faces in the positive z direction, similarly to Mixamo animation data and readyplayerme avatars.
static void rotateVRMMesh(BatchedMesh& mesh)
{
	conPrint("Rotating VRM mesh");

	const BatchedMesh::VertAttribute& pos = mesh.getAttribute(BatchedMesh::VertAttribute_Position);
	if(pos.component_type != BatchedMesh::ComponentType_Float)
		throw glare::Exception("unhandled pos component type in rotateVRMMesh()");

	const size_t num_verts = mesh.numVerts();
	const size_t vert_stride_B = mesh.vertexSize();

	js::AABBox new_aabb_os = js::AABBox::emptyAABBox();

	for(size_t i=0; i<num_verts; ++i)
	{
		Vec3f v;
		std::memcpy(&v, &mesh.vertex_data[vert_stride_B * i + pos.offset_B], sizeof(Vec3f));
		
		const Vec3f new_v(-v.x, v.y, -v.z);
		
		std::memcpy(&mesh.vertex_data[vert_stride_B * i + pos.offset_B], &new_v, sizeof(Vec3f));
		
		new_aabb_os.enlargeToHoldPoint(new_v.toVec4fPoint());
	}

	const BatchedMesh::VertAttribute* normal_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Normal);
	if(normal_attr)
	{
		if(normal_attr->component_type == BatchedMesh::ComponentType_PackedNormal)
		{
			for(size_t i=0; i<num_verts; ++i)
			{
				uint32 packed_normal;
				std::memcpy(&packed_normal, &mesh.vertex_data[vert_stride_B * i + normal_attr->offset_B], sizeof(uint32));

				Vec4f n = batchedMeshUnpackNormal(packed_normal); // TODO: do this with integer manipulation instead of floats? Will avoid possible rounding error.
				Vec4f new_n(-n[0], n[1], -n[2], 0);

				const uint32 new_packed_normal = batchedMeshPackNormal(new_n);
				std::memcpy(&mesh.vertex_data[vert_stride_B * i + normal_attr->offset_B], &new_packed_normal, sizeof(uint32));
			}
		}
		else
			throw glare::Exception("unhandled normal component type in rotateVRMMesh()");
	}

	// Update animation data
	for(size_t i=0; i<mesh.animation_data.nodes.size(); ++i)
	{
		assert(mesh.animation_data.nodes[i].inverse_bind_matrix.getUpperLeftMatrix() == Matrix3f::identity());
		mesh.animation_data.nodes[i].inverse_bind_matrix.e[12] *= -1.f; // Negate x translation
		mesh.animation_data.nodes[i].inverse_bind_matrix.e[14] *= -1.f; // Negate z translation

		//mesh.animation_data.nodes[i].default_node_hierarchical_to_world.e[12] *= -1.f; // Negate x translation
		//mesh.animation_data.nodes[i].default_node_hierarchical_to_world.e[14] *= -1.f; // Negate z translation

		// Negate x and z components of trans
		mesh.animation_data.nodes[i].trans = Vec4f(
			-mesh.animation_data.nodes[i].trans[0], 
			mesh.animation_data.nodes[i].trans[1], 
			-mesh.animation_data.nodes[i].trans[2],
			0);
	}
	

	mesh.aabb_os = new_aabb_os;
}


// We don't have a material file, just the model file:
GLObjectRef ModelLoading::makeGLObjectForModelFile(
	VertexBufferAllocator& vert_buf_allocator,
	glare::TaskManager& task_manager, 
	const std::string& model_path,
	BatchedMeshRef& mesh_out,
	WorldObject& loaded_object_out
)
{
	if(hasExtension(model_path, "vox"))
	{
		VoxFileContents vox_contents;
		FormatDecoderVox::loadModel(model_path, vox_contents);

		// Convert voxels
		if(vox_contents.models.empty())
			throw glare::Exception("No model in vox file.");

		const VoxModel& model = vox_contents.models[0];

		// We will offset the voxel positions so that the origin is in the middle at the bottom of the voxel AABB.
		const int x_offset = (int)-model.aabb.centroid()[0];
		const int y_offset = (int)-model.aabb.centroid()[1];

		loaded_object_out.getDecompressedVoxels().resize(model.voxels.size());
		for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
		{
			loaded_object_out.getDecompressedVoxels()[i].pos = Vec3<int>(model.voxels[i].x + x_offset, model.voxels[i].y + y_offset, model.voxels[i].z);
			loaded_object_out.getDecompressedVoxels()[i].mat_index = model.voxels[i].mat_index;
		}

		loaded_object_out.compressVoxels();

		// Convert materials
		loaded_object_out.materials.resize(vox_contents.used_materials.size());
		for(size_t i=0; i<loaded_object_out.materials.size(); ++i)
		{
			loaded_object_out.materials[i] = new WorldMaterial();
			loaded_object_out.materials[i]->colour_rgb = Colour3f(
				vox_contents.used_materials[i].col_from_palette[0], 
				vox_contents.used_materials[i].col_from_palette[1], 
				vox_contents.used_materials[i].col_from_palette[2]);
		}

		// Scale down voxels so model isn't too large.
		const float use_scale = getScaleForVoxModel(model.aabb);

		// Make opengl object
		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = Matrix4f::uniformScaleMatrix(use_scale);

		Reference<RayMesh> raymesh;
		const int subsample_factor = 1;
		ob->mesh_data = ModelLoading::makeModelForVoxelGroup(loaded_object_out.getDecompressedVoxelGroup(), subsample_factor, ob->ob_to_world_matrix, task_manager, &vert_buf_allocator, /*do opengl stuff=*/true, raymesh);

		ob->materials.resize(loaded_object_out.materials.size());
		for(size_t i=0; i<loaded_object_out.materials.size(); ++i)
		{
			setGLMaterialFromWorldMaterialWithLocalPaths(*loaded_object_out.materials[i], ob->materials[i]);
		}

		loaded_object_out.scale.set(use_scale, use_scale, use_scale);

		mesh_out = NULL;
		return ob;
	}
	else if(hasExtension(model_path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/true, mats);

		checkValidAndSanitiseMesh(*mesh);

		// Convert model coordinates to z up
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			mesh->vert_positions[i] = Indigo::Vec3f(mesh->vert_positions[i].x, -mesh->vert_positions[i].z, mesh->vert_positions[i].y);

		// Also normalise normals to avoid problems encoding into GL_INT_2_10_10_10_REV format.
		for(size_t i=0; i<mesh->vert_normals.size(); ++i)
			mesh->vert_normals[i] = normalise(Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y));

		// Automatically scale object down until it is < x m across
		scaleMesh(*mesh);

		// Now that vertices have been modified, recompute AABB
		mesh->endOfModel();

		// Get smallest z coord
		float min_z = std::numeric_limits<float>::max();
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			min_z = myMin(min_z, mesh->vert_positions[i].z);

		// Move object so that it lies on the z=0 (ground) plane
		const Matrix4f use_matrix = Matrix4f::identity(); // Matrix4f::translationMatrix(0, 0, -min_z)* ob_to_world_matrix;

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = use_matrix;
		ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, /*skip opengl calls=*/false);

		ob->materials.resize(mesh->num_materials_referenced);
		loaded_object_out.materials/*loaded_materials_out*/.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			loaded_object_out.materials[i] = new WorldMaterial();

			// Have we parsed such a material from the .mtl file?
			bool found_mat = false;
			for(size_t z=0; z<mats.materials.size(); ++z)
				if(mats.materials[z].name == toStdString(mesh->used_materials[i]))
				{
					const std::string tex_path = (!mats.materials[z].map_Kd.path.empty()) ? FileUtils::join(FileUtils::getDirectory(mats.mtl_file_path), mats.materials[z].map_Kd.path) : "";

					ob->materials[i].albedo_rgb = mats.materials[z].Kd;
					ob->materials[i].tex_path = tex_path;
					ob->materials[i].roughness = 0.5f;//mats.materials[z].Ns_exponent; // TODO: convert
					ob->materials[i].alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);

					loaded_object_out.materials[i]->colour_rgb = mats.materials[z].Kd;
					loaded_object_out.materials[i]->colour_texture_url = tex_path;
					loaded_object_out.materials[i]->opacity = ScalarVal(ob->materials[i].alpha);
					loaded_object_out.materials[i]->roughness = ScalarVal(0.5f);

					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				//ob->materials[i].albedo_tex_path = "resources/obstacle.png";
				ob->materials[i].roughness = 0.5f;

				//loaded_materials_out[i]->colour_texture_url = "resources/obstacle.png";
				loaded_object_out.materials[i]->opacity = ScalarVal(1.f);
				loaded_object_out.materials[i]->roughness = ScalarVal(0.5f);
			}

			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = new BatchedMesh();
		mesh_out->buildFromIndigoMesh(*mesh);
		return ob;
	}
	else if(hasExtension(model_path, "gltf") || hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
	{
		Timer timer;
		
		GLTFLoadedData gltf_data;
		BatchedMeshRef batched_mesh = hasExtension(model_path, "gltf") ? 
			FormatDecoderGLTF::loadGLTFFile(model_path, gltf_data) : 
			FormatDecoderGLTF::loadGLBFile(model_path, gltf_data);
		
		conPrint("Loaded GLTF model in " + timer.elapsedString());

		checkValidAndSanitiseMesh(*batched_mesh);

		if(batched_mesh->animation_data.vrm_data.nonNull())
			rotateVRMMesh(*batched_mesh);

		const float scale = getScaleForMesh(*batched_mesh);

		loaded_object_out.pos = Vec3d(0,0,0);
		loaded_object_out.scale = Vec3f(scale);
		loaded_object_out.axis = Vec3f(1,0,0);
		loaded_object_out.angle = Maths::pi_2<float>();
		loaded_object_out.translation = Vec4f(0.f);

		GLObjectRef gl_ob = new GLObject();
		gl_ob->ob_to_world_matrix = obToWorldMatrix(loaded_object_out);
		gl_ob->mesh_data = GLMeshBuilding::buildBatchedMesh(&vert_buf_allocator, batched_mesh, /*skip_opengl_calls=*/false, /*instancing_matrix_data=*/NULL);

		gl_ob->mesh_data->animation_data = batched_mesh->animation_data;// gltf_data.anim_data;

		const size_t bmesh_num_mats_referenced = batched_mesh->numMaterialsReferenced();
		if(gltf_data.materials.materials.size() < bmesh_num_mats_referenced)
			throw glare::Exception("mats.materials had incorrect size.");

		gl_ob->materials.resize(bmesh_num_mats_referenced);
		loaded_object_out.materials.resize(bmesh_num_mats_referenced);
		for(uint32 i=0; i<bmesh_num_mats_referenced; ++i)
		{
			loaded_object_out.materials[i] = new WorldMaterial();

			const std::string tex_path = gltf_data.materials.materials[i].diffuse_map.path;
			const std::string metallic_roughness_tex_path = gltf_data.materials.materials[i].metallic_roughness_map.path;

			// NOTE: gltf has (0,0) at the upper left of the image, as opposed to the Indigo/substrata/opengl convention of (0,0) being at the lower left
			// (See https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#images)
			// Therefore we need to negate the y coord.
			// For the gl_ob, there would usually be another negation, so the two cancel out.

			gl_ob->materials[i].albedo_rgb = gltf_data.materials.materials[i].diffuse;
			gl_ob->materials[i].tex_path = tex_path;
			gl_ob->materials[i].metallic_roughness_tex_path = metallic_roughness_tex_path;
			gl_ob->materials[i].roughness = gltf_data.materials.materials[i].roughness;
			gl_ob->materials[i].alpha = gltf_data.materials.materials[i].alpha;
			gl_ob->materials[i].transparent = gltf_data.materials.materials[i].alpha < 1.0f;
			gl_ob->materials[i].metallic_frac = gltf_data.materials.materials[i].metallic;

			loaded_object_out.materials[i]->colour_rgb = gltf_data.materials.materials[i].diffuse;
			loaded_object_out.materials[i]->colour_texture_url = tex_path;
			loaded_object_out.materials[i]->roughness.texture_url = metallic_roughness_tex_path; // HACK: just assign to roughness URL
			loaded_object_out.materials[i]->opacity = ScalarVal(gl_ob->materials[i].alpha);
			loaded_object_out.materials[i]->roughness.val = gltf_data.materials.materials[i].roughness;
			loaded_object_out.materials[i]->opacity.val = gltf_data.materials.materials[i].alpha;
			loaded_object_out.materials[i]->metallic_fraction.val = gltf_data.materials.materials[i].metallic;
			loaded_object_out.materials[i]->tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = batched_mesh;
		return gl_ob;
	}
	else if(hasExtension(model_path, "stl"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			FormatDecoderSTL::streamModel(model_path, *mesh, 1.f);
			checkValidAndSanitiseMesh(*mesh);

			// Automatically scale object down until it is < x m across
			scaleMesh(*mesh);

			// Now that vertices have been modified, recompute AABB
			mesh->endOfModel();

			// Get smallest z coord
			float min_z = std::numeric_limits<float>::max();
			for(size_t i=0; i<mesh->vert_positions.size(); ++i)
				min_z = myMin(min_z, mesh->vert_positions[i].z);

			// Move object so that it lies on the z=0 (ground) plane
			const Matrix4f use_matrix = Matrix4f::translationMatrix(0, 0, -min_z);// *ob_to_world_matrix;

			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = use_matrix;
			ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, false);

			ob->materials.resize(mesh->num_materials_referenced);
			loaded_object_out.materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

				loaded_object_out.materials[i] = new WorldMaterial();
			}

			mesh_out = new BatchedMesh();
			mesh_out->buildFromIndigoMesh(*mesh);
			return ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);

			checkValidAndSanitiseMesh(*mesh);

			// Automatically scale object down until it is < x m across
			scaleMesh(*mesh);
			
			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
			ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, /*skip_opengl_calls=*/false);

			ob->materials.resize(mesh->num_materials_referenced);
			loaded_object_out.materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].tex_path = "resources/obstacle.png";
				ob->materials[i].roughness = 0.5f;
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

				loaded_object_out.materials[i] = new WorldMaterial();
				//loaded_object_out.materials[i]->colour_texture_url = "resources/obstacle.png";
				loaded_object_out.materials[i]->opacity = ScalarVal(1.f);
				loaded_object_out.materials[i]->roughness = ScalarVal(0.5f);
			}
			
			mesh_out = new BatchedMesh();
			mesh_out->buildFromIndigoMesh(*mesh);
			return ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}
	}
	else if(hasExtension(model_path, "bmesh"))
	{
		BatchedMeshRef bmesh = new BatchedMesh();
		BatchedMesh::readFromFile(model_path, *bmesh);

		checkValidAndSanitiseMesh(*bmesh);

		// Automatically scale object down until it is < x m across
		//scaleMesh(*bmesh);

		GLObjectRef gl_ob = new GLObject();
		gl_ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
		gl_ob->mesh_data = GLMeshBuilding::buildBatchedMesh(&vert_buf_allocator, bmesh, /*skip_opengl_calls=*/false, /*instancing_matrix_data=*/NULL);

		gl_ob->mesh_data->animation_data = bmesh->animation_data;

		const size_t num_mats = bmesh->numMaterialsReferenced();
		gl_ob->materials.resize(num_mats);
		loaded_object_out.materials.resize(num_mats);
		for(uint32 i=0; i<gl_ob->materials.size(); ++i)
		{
			// Assign dummy mat
			gl_ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			gl_ob->materials[i].tex_path = "resources/obstacle.png";
			gl_ob->materials[i].roughness = 0.5f;
			gl_ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

			loaded_object_out.materials[i] = new WorldMaterial();
			//loaded_object_out.materials[i]->colour_texture_url = "resources/obstacle.png";
			loaded_object_out.materials[i]->opacity = ScalarVal(1.f);
			loaded_object_out.materials[i]->roughness = ScalarVal(0.5f);
		}

		mesh_out = bmesh;
		return gl_ob;
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));
}


GLObjectRef ModelLoading::makeImageCube(VertexBufferAllocator& vert_buf_allocator, glare::TaskManager& task_manager, 
	const std::string& image_path, int im_w, int im_h,
	BatchedMeshRef& mesh_out,
	WorldObject& loaded_object_out)
{
	float use_w, use_h;
	if(im_w > im_h)
	{
		use_w = 1;
		use_h = (float)im_h / (float)im_w;
	}
	else
	{
		use_h = 1;
		use_w = (float)im_w / (float)im_h;
	}

	MeshBuilding::MeshBuildingResults results = MeshBuilding::makeImageCube(task_manager, vert_buf_allocator);

	const float depth = 0.02f;
	const Matrix4f use_matrix = Matrix4f::scaleMatrix(use_w, depth, use_h) * Matrix4f::translationMatrix(-0.5f, 0, 0); // transform in gl preview

	GLObjectRef preview_gl_ob = new GLObject();
	preview_gl_ob->ob_to_world_matrix = use_matrix;
	preview_gl_ob->mesh_data = results.opengl_mesh_data;
	preview_gl_ob->materials.resize(2);

	// Front/back face material:
	preview_gl_ob->materials[0].albedo_rgb = Colour3f(0.9f);
	preview_gl_ob->materials[0].tex_path = image_path;
	preview_gl_ob->materials[0].roughness = 0.5f;
	preview_gl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1);

	// Edge material:
	preview_gl_ob->materials[1].albedo_rgb = Colour3f(0.7f);
	preview_gl_ob->materials[1].roughness = 0.5f;
	preview_gl_ob->materials[1].tex_matrix = Matrix2f(1, 0, 0, -1);


	loaded_object_out.scale = Vec3f(use_w, depth, use_h);
	loaded_object_out.materials.resize(2);

	loaded_object_out.materials[0] = new WorldMaterial();
	loaded_object_out.materials[0]->colour_rgb = Colour3f(0.9f);
	loaded_object_out.materials[0]->opacity = ScalarVal(1.f);
	loaded_object_out.materials[0]->roughness = ScalarVal(0.5f);
	loaded_object_out.materials[0]->colour_texture_url = image_path;

	loaded_object_out.materials[1] = new WorldMaterial();
	loaded_object_out.materials[1]->colour_rgb = Colour3f(0.7f);
	loaded_object_out.materials[1]->opacity = ScalarVal(1.f);
	loaded_object_out.materials[1]->roughness = ScalarVal(0.5f);

	mesh_out = new BatchedMesh();
	mesh_out->buildFromIndigoMesh(*results.indigo_mesh);

	return preview_gl_ob;
}


GLObjectRef ModelLoading::makeGLObjectForMeshDataAndMaterials(const Reference<OpenGLMeshRenderData> gl_meshdata, //size_t num_materials_referenced,
	int ob_lod_level, const std::vector<WorldMaterialRef>& materials, const std::string& lightmap_url,
	ResourceManager& resource_manager,
	const Matrix4f& ob_to_world_matrix)
{
	// Make the GLObject
	GLObjectRef ob = new GLObject();
	ob->ob_to_world_matrix = ob_to_world_matrix;
	ob->mesh_data = gl_meshdata;

	ob->materials.resize(gl_meshdata->num_materials_referenced);
	for(uint32 i=0; i<ob->materials.size(); ++i)
	{
		if(i < materials.size())
		{
			setGLMaterialFromWorldMaterial(*materials[i], ob_lod_level, lightmap_url, resource_manager, ob->materials[i]);
		}
		else
		{
			// Assign dummy mat
			ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			ob->materials[i].tex_path = "resources/obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}
	}

	// Show LOD level by tinting materials
	if(false)
	{
		const int lod_level = 0;// StringUtils::containsString(lod_model_URL, "_lod1") ? 1 : (StringUtils::containsString(lod_model_URL, "_lod2") ? 2 : 0);
		if(lod_level == 1)
		{
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				ob->materials[i].albedo_rgb.r *= 0.6;
				ob->materials[i].albedo_rgb.b *= 0.6;
			}
		}
		else if(lod_level == 2)
		{
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				ob->materials[i].albedo_rgb.g *= 0.6;
				ob->materials[i].albedo_rgb.b *= 0.6;
			}
		}
	}
	
	return ob;
}


void ModelLoading::setMaterialTexPathsForLODLevel(GLObject& gl_ob, int ob_lod_level, const std::vector<WorldMaterialRef>& materials,
	const std::string& lightmap_url, ResourceManager& resource_manager)
{
	for(size_t i=0; i<gl_ob.materials.size(); ++i)
	{
		if(i < materials.size())
			setGLMaterialFromWorldMaterial(*materials[i], ob_lod_level, lightmap_url, resource_manager, gl_ob.materials[i]);
	}
}


bool ModelLoading::hasSupportedModelExtension(const std::string& path)
{
	const string_view extension = getExtensionStringView(path);

	return
		extension == "vox" ||
		extension == "obj" ||
		extension == "stl" ||
		extension == "gltf" ||
		extension == "glb" ||
		extension == "vrm" ||
		extension == "igmesh" ||
		extension == "bmesh";
}


Reference<OpenGLMeshRenderData> ModelLoading::makeGLMeshDataAndRayMeshForModelURL(const std::string& lod_model_URL,
	ResourceManager& resource_manager, glare::TaskManager& task_manager, VertexBufferAllocator* vert_buf_allocator,
	bool skip_opengl_calls, Reference<RayMesh>& raymesh_out)
{
	// Load Indigo mesh and OpenGL mesh data, or get from mesh_manager if already loaded.
	size_t num_materials_referenced = 0;
	Reference<OpenGLMeshRenderData> gl_meshdata;
	Reference<RayMesh> raymesh;

	// Load mesh from disk:
	const std::string model_path = resource_manager.pathForURL(lod_model_URL);

	BatchedMeshRef batched_mesh = new BatchedMesh();

	if(hasExtension(model_path, "obj"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		MLTLibMaterials mats;
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws glare::Exception on failure.

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "stl"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		FormatDecoderSTL::streamModel(model_path, *mesh, 1.f);

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "gltf"))
	{
		GLTFLoadedData gltf_data;
		batched_mesh = FormatDecoderGLTF::loadGLTFFile(model_path, gltf_data);
	}
	else if(hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
	{
		GLTFLoadedData gltf_data;
		batched_mesh = FormatDecoderGLTF::loadGLBFile(model_path, gltf_data);
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		try
		{
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "bmesh"))
	{
		BatchedMesh::readFromFile(model_path, *batched_mesh);
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));


	checkValidAndSanitiseMesh(*batched_mesh); // Throws glare::Exception on invalid mesh.

	if(hasExtension(model_path, "gltf") || hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
		if(batched_mesh->animation_data.vrm_data.nonNull())
			rotateVRMMesh(*batched_mesh);

	gl_meshdata = GLMeshBuilding::buildBatchedMesh(vert_buf_allocator, batched_mesh, /*skip opengl calls=*/skip_opengl_calls, /*instancing_matrix_data=*/NULL);

	gl_meshdata->animation_data = batched_mesh->animation_data;

	// Build RayMesh from our batched mesh (used for physics + picking)
	raymesh = new RayMesh(/*name=*/FileUtils::getFilename(model_path), false);
	raymesh->fromBatchedMesh(*batched_mesh);

	Geometry::BuildOptions options;
	options.compute_is_planar = false;
	DummyShouldCancelCallback should_cancel_callback;
	StandardPrintOutput print_output;
	raymesh->build(options, should_cancel_callback, print_output, false, task_manager);

	num_materials_referenced = batched_mesh->numMaterialsReferenced();

	gl_meshdata->num_materials_referenced = num_materials_referenced;

	raymesh_out = raymesh;
	return gl_meshdata;
}


struct ModelLoadingTakeFirstElement
{
	inline uint32 operator() (const std::pair<uint32, uint32>& pair) const { return pair.first; }
};


struct ModelLoadingVertKey
{
	Indigo::Vec3f pos;
	Indigo::Vec2f uv0;
	//Indigo::Vec2f uv1;

	inline bool operator == (const ModelLoadingVertKey& b) const
	{
		return pos == b.pos && uv0 == b.uv0/* && uv1 == b.uv1*/;
	}
	inline bool operator != (const ModelLoadingVertKey& b) const
	{
		return pos != b.pos || uv0 != b.uv0/* || uv1 != b.uv1*/;
	}
};


struct ModelLoadingVertKeyHash
{
	size_t operator() (const ModelLoadingVertKey& v) const
	{
		return hashBytes((const uint8*)&v.pos, sizeof(Vec3f));
	}
};


// Requires dest and src to be 4-byte aligned.
// size is in bytes.
inline static void copyUInt32s(void* const dest, const void* const src, size_t size_B)
{
	/*assert(((uint64)dest % 4 == 0) && ((uint64)src % 4 == 0));

	const size_t num_uints = size_B / 4;

	for(size_t z=0; z<num_uints; ++z)
		*((uint32*)dest + z) = *((const uint32*)src + z);*/
	std::memcpy(dest, src, size_B); // Not necessarily 4 byte aligned now so just use memcpy.
}


// TODO: can probably avoid sorting triangles by material in this method (should already be sorted)
static Reference<OpenGLMeshRenderData> buildVoxelOpenGLMeshData(const Indigo::Mesh& mesh_/*, const Vec3<int>& min_vert_coords, const Vec3<int>& max_vert_coords*/)
{
	const Indigo::Mesh* const mesh				= &mesh_;
	const Indigo::Triangle* const tris			= mesh->triangles.data();
	const size_t num_tris						= mesh->triangles.size();
	const Indigo::Vec3f* const vert_positions	= mesh->vert_positions.data();
	const size_t vert_positions_size			= mesh->vert_positions.size();
	const Indigo::Vec2f* const uv_pairs			= mesh->uv_pairs.data();
	const size_t uvs_size						= mesh->uv_pairs.size();

	const bool mesh_has_uvs						= mesh->num_uv_mappings > 0;
	//const bool mesh_has_uv1						= mesh->num_uv_mappings > 1;
	const uint32 num_uv_sets					= mesh->num_uv_mappings;

	// If we have a UV set, it will be the lightmap UVs.

	// Work out the min and max vertex coordinates, to see if we can store in an int8 or int16.
	const int min_voxel_coord = myMin((int)mesh_.aabb_os.bound[0].x, (int)mesh_.aabb_os.bound[0].y, (int)mesh_.aabb_os.bound[0].z);
	const int max_voxel_coord = myMax((int)mesh_.aabb_os.bound[1].x, (int)mesh_.aabb_os.bound[1].y, (int)mesh_.aabb_os.bound[1].z);

	size_t pos_size;
	GLenum pos_gl_type;
	if(min_voxel_coord >= -128 && max_voxel_coord < 128)
	{
		pos_size = sizeof(int8) * 3;
		pos_gl_type = GL_BYTE;
	}
	else if(min_voxel_coord >= -32768 && max_voxel_coord < 32768)
	{
		pos_size = sizeof(int16) * 3;
		pos_gl_type = GL_SHORT;
	}
	else
	{
		pos_size = sizeof(float) * 3;
		pos_gl_type = GL_FLOAT;
	}

	const size_t uv0_size = sizeof(half)  * 2;
	//const GLenum uv0_gl_type = GL_HALF_FLOAT;


	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	mesh_data->has_shading_normals = false;
	mesh_data->has_uvs = mesh_has_uvs;


	// If UVs are somewhat small in magnitude, use GL_HALF_FLOAT instead of GL_FLOAT.
	// If the magnitude is too high we can get artifacts if we just use half precision.
	//const bool use_half_uv1 = true; // TEMP canUseHalfUVs(mesh); // Just for UV1

	//const size_t packed_uv1_size = use_half_uv1 ? sizeof(half)*2 : sizeof(float)*2;

	/*
	Vertex data layout is
	position [always present]
	uv_0     [optional]
	uv_1     [optional]
	*/
	const size_t uv0_offset         = 0          + pos_size;
	const size_t uv1_offset         = uv0_offset + uv0_size;
	const size_t num_bytes_per_vert = uv1_offset;// +(mesh_has_uv1 ? packed_uv1_size : 0);

	js::Vector<uint8, 16>& vert_data = mesh_data->vert_data;
	vert_data.reserve(mesh->vert_positions.size() * num_bytes_per_vert);

	js::Vector<uint32, 16> uint32_indices(mesh->triangles.size() * 3 + mesh->quads.size() * 6);

	size_t vert_index_buffer_i = 0; // Current write index into vert_index_buffer
	size_t next_merged_vert_i = 0;
	size_t last_pass_start_index = 0;
	uint32 current_mat_index = std::numeric_limits<uint32>::max();

	ModelLoadingVertKey empty_key;
	empty_key.pos = Indigo::Vec3f(std::numeric_limits<float>::infinity());
	empty_key.uv0 = Indigo::Vec2f(0.f);
	//empty_key.uv1 = Indigo::Vec2f(0.f);
	HashMapInsertOnly2<ModelLoadingVertKey, uint32, ModelLoadingVertKeyHash> vert_map(empty_key, // Map from vert data to merged index
		/*expected_num_items=*/mesh->vert_positions.size()); 


	if(mesh->triangles.size() > 0)
	{
		// Create list of triangle references sorted by material index
		js::Vector<std::pair<uint32, uint32>, 16> unsorted_tri_indices(num_tris);
		js::Vector<std::pair<uint32, uint32>, 16> tri_indices(num_tris); // Sorted by material

		for(uint32 t = 0; t < num_tris; ++t)
			unsorted_tri_indices[t] = std::make_pair(tris[t].tri_mat_index, t);

		Sort::serialCountingSort(/*in=*/unsorted_tri_indices.data(), /*out=*/tri_indices.data(), num_tris, ModelLoadingTakeFirstElement());

		for(uint32 t = 0; t < num_tris; ++t)
		{
			// If we've switched to a new material then start a new triangle range
			if(tri_indices[t].first != current_mat_index)
			{
				if(t > 0) // Don't add zero-length passes.
				{
					OpenGLBatch batch;
					batch.material_index = current_mat_index;
					batch.prim_start_offset = (uint32)(last_pass_start_index); // Store index for now, will be adjusted to byte offset later.
					batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
					mesh_data->batches.push_back(batch);
				}
				last_pass_start_index = vert_index_buffer_i;
				current_mat_index = tri_indices[t].first;
			}

			const Indigo::Triangle& tri = tris[tri_indices[t].second];
			for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
			{
				const uint32 pos_i		= tri.vertex_indices[i];
				const uint32 base_uv_i	= tri.uv_indices[i];
				const uint32 uv_i = base_uv_i * num_uv_sets; // Index of UV for UV set 0.
				if(pos_i >= vert_positions_size)
					throw glare::Exception("vert index out of bounds");
				if(mesh_has_uvs && uv_i >= uvs_size)
					throw glare::Exception("UV index out of bounds");

				// Look up merged vertex
				const Indigo::Vec2f uv0 = mesh_has_uvs ? uv_pairs[uv_i    ] : Indigo::Vec2f(0.f);
				//const Indigo::Vec2f uv1 = mesh_has_uv1 ? uv_pairs[uv_i + 1] : Indigo::Vec2f(0.f);

				ModelLoadingVertKey key;
				key.pos = vert_positions[pos_i];
				key.uv0 = uv0;
				//key.uv1 = uv1;

				const auto res = vert_map.insert(std::make_pair(key, (uint32)next_merged_vert_i)); // Insert new (key, value) pair, or return iterator to existing one.
				const uint32 merged_v_index = res.first->second; // Get existing or new (key, vert_i) pair, then access vert_i.
				if(res.second) // If was inserted:
				{
					next_merged_vert_i++;
					const size_t cur_size = vert_data.size();
					vert_data.resize(cur_size + num_bytes_per_vert);
					if(pos_gl_type == GL_BYTE)
					{
						const int8 pos[3] = { (int8)round(vert_positions[pos_i].x), (int8)round(vert_positions[pos_i].y), (int8)round(vert_positions[pos_i].z) };
						std::memcpy(&vert_data[cur_size], pos, sizeof(int8) * 3);
					}
					else if(pos_gl_type == GL_SHORT)
					{
						const int16 pos[3] = { (int16)round(vert_positions[pos_i].x), (int16)round(vert_positions[pos_i].y), (int16)round(vert_positions[pos_i].z) };
						std::memcpy(&vert_data[cur_size], pos, sizeof(int16) * 3);
					}
					else
					{
						assert(pos_gl_type == GL_FLOAT);
						copyUInt32s(&vert_data[cur_size], &vert_positions[pos_i].x, sizeof(Indigo::Vec3f)); // Copy vert position
					}

					if(mesh_has_uvs)
					{
						// Copy uv_0
						const half half_uv[2] = { half(uv0.x),  half(uv0.y) };
						copyUInt32s(&vert_data[cur_size + uv0_offset], half_uv, 4);

						// Copy uv_1
						/*if(mesh_has_uv1)
						{
							if(use_half_uv1)
							{
								const half half_uv[2] = { half(uv1.x),  half(uv1.y) };
								copyUInt32s(&vert_data[cur_size + uv1_offset], half_uv, 4);
							}
							else
								copyUInt32s(&vert_data[cur_size + uv1_offset], &uv1.x, sizeof(Indigo::Vec2f));
						}*/
					}
				}

				uint32_indices[vert_index_buffer_i++] = (uint32)merged_v_index;
			}
		}
	}

	// Build last pass data that won't have been built yet.
	OpenGLBatch batch;
	batch.material_index = current_mat_index;
	batch.prim_start_offset = (uint32)(last_pass_start_index); // Store index for now, will be adjusted to byte offset later.
	batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
	mesh_data->batches.push_back(batch);

	const size_t num_merged_verts = next_merged_vert_i;

	// Build index data
	const size_t num_indices = uint32_indices.size();

	if(num_merged_verts < 128)
	{
		mesh_data->index_type = GL_UNSIGNED_BYTE;

		mesh_data->vert_index_buffer_uint8.resize(num_indices);

		uint8* const dest_indices = mesh_data->vert_index_buffer_uint8.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = (uint8)uint32_indices[i];
	}
	else if(num_merged_verts < 32768)
	{
		mesh_data->index_type = GL_UNSIGNED_SHORT;

		mesh_data->vert_index_buffer_uint16.resize(num_indices);

		uint16* const dest_indices = mesh_data->vert_index_buffer_uint16.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = (uint16)uint32_indices[i];

		// Adjust batch prim_start_offset, from index to byte offset
		for(size_t i=0; i<mesh_data->batches.size(); ++i)
			mesh_data->batches[i].prim_start_offset *= 2;
	}
	else
	{
		mesh_data->index_type = GL_UNSIGNED_INT;

		mesh_data->vert_index_buffer.resize(num_indices);

		uint32* const dest_indices = mesh_data->vert_index_buffer.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = uint32_indices[i];

		// Adjust batch prim_start_offset, from index to byte offset
		for(size_t i=0; i<mesh_data->batches.size(); ++i)
			mesh_data->batches[i].prim_start_offset *= 4;
	}

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = pos_gl_type;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)num_bytes_per_vert;
	pos_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(pos_attrib);

	VertexAttrib normal_attrib;
	normal_attrib.enabled = false;
	normal_attrib.num_comps = 3;
	normal_attrib.type = GL_FLOAT;
	normal_attrib.normalised = false;
	normal_attrib.stride = (uint32)num_bytes_per_vert;
	normal_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(normal_attrib);

	VertexAttrib uv_attrib;
	uv_attrib.enabled = false;
	uv_attrib.num_comps = 2;
	uv_attrib.type = GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = (uint32)num_bytes_per_vert;
	uv_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(uv_attrib);

	VertexAttrib colour_attrib;
	colour_attrib.enabled = false;
	colour_attrib.num_comps = 3;
	colour_attrib.type = GL_FLOAT;
	colour_attrib.normalised = false;
	colour_attrib.stride = (uint32)num_bytes_per_vert;
	colour_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(colour_attrib);

	if(num_uv_sets >= 1)
	{
		VertexAttrib lightmap_uv_attrib;
		lightmap_uv_attrib.enabled = true;
		lightmap_uv_attrib.num_comps = 2;
		lightmap_uv_attrib.type = GL_HALF_FLOAT;
		lightmap_uv_attrib.normalised = false;
		lightmap_uv_attrib.stride = (uint32)num_bytes_per_vert;
		lightmap_uv_attrib.offset = (uint32)uv0_offset;
		mesh_data->vertex_spec.attributes.push_back(lightmap_uv_attrib);
	}
	/*if(num_uv_sets >= 2)
	{
		VertexAttrib uv_attrib;
		uv_attrib.enabled = true;
		uv_attrib.num_comps = 2;
		uv_attrib.type = use_half_uv1 ? GL_HALF_FLOAT : GL_FLOAT;
		uv_attrib.normalised = false;
		uv_attrib.stride = (uint32)num_bytes_per_vert;
		uv_attrib.offset = (uint32)uv1_offset;
		mesh_data->vertex_spec.attributes.push_back(uv_attrib);
	}*/

	mesh_data->aabb_os = js::AABBox(
		Vec4f(mesh_.aabb_os.bound[0].x, mesh_.aabb_os.bound[0].y, mesh_.aabb_os.bound[0].z, 1.f),
		Vec4f(mesh_.aabb_os.bound[1].x, mesh_.aabb_os.bound[1].y, mesh_.aabb_os.bound[1].z, 1.f)
	);

	return mesh_data;
}


Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, int subsample_factor, const Matrix4f& ob_to_world, 
	glare::TaskManager& task_manager, VertexBufferAllocator* vert_buf_allocator, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out)
{
	//Timer timer;

	Indigo::MeshRef indigo_mesh = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(voxel_group, subsample_factor, /*generate_shading_normals=*/false);
	// We will compute geometric normals in the opengl shader, so don't need to compute them here.

	// UV unwrap it:
	StandardPrintOutput print_output;
	
	const js::AABBox aabb_os(
		Vec4f(indigo_mesh->aabb_os.bound[0].x, indigo_mesh->aabb_os.bound[0].y, indigo_mesh->aabb_os.bound[0].z, 1.f),
		Vec4f(indigo_mesh->aabb_os.bound[1].x, indigo_mesh->aabb_os.bound[1].y, indigo_mesh->aabb_os.bound[1].z, 1.f)
	);
	const js::AABBox aabb_ws = aabb_os.transformedAABB(ob_to_world);

	const int clamped_side_res = WorldObject::getLightMapSideResForAABBWS(aabb_ws);

	const float normed_margin = 2.f / clamped_side_res;
	UVUnwrapper::build(*indigo_mesh, ob_to_world, print_output, normed_margin); // Adds UV set to indigo_mesh.

	// Convert Indigo mesh to opengl data
	Reference<OpenGLMeshRenderData> mesh_data = buildVoxelOpenGLMeshData(*indigo_mesh);

	// Build RayMesh from our indigo mesh (used for physics + picking)
	raymesh_out = new RayMesh("voxelmesh", /*enable_shading_normals=*/false);
	raymesh_out->fromIndigoMeshForPhysics(*indigo_mesh);

	// Build raymesh acceleration structure
	Geometry::BuildOptions options;
	options.compute_is_planar = false;
	DummyShouldCancelCallback should_cancel_callback;
	raymesh_out->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);

	// Load rendering data into GPU mem if requested.
	if(do_opengl_stuff)
	{
		if(!mesh_data->vert_index_buffer_uint8.empty())
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer_uint8.data(), mesh_data->vert_index_buffer_uint8.dataSizeBytes());
			assert(mesh_data->index_type == GL_UNSIGNED_BYTE);
		}
		else if(!mesh_data->vert_index_buffer_uint16.empty())
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer_uint16.data(), mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			assert(mesh_data->index_type == GL_UNSIGNED_SHORT);
		}
		else
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer.data(), mesh_data->vert_index_buffer.dataSizeBytes());
			assert(mesh_data->index_type == GL_UNSIGNED_INT);
		}

		mesh_data->vbo_handle = vert_buf_allocator->allocate(mesh_data->vertex_spec, mesh_data->vert_data.data(), mesh_data->vert_data.dataSizeBytes());

#if DO_INDIVIDUAL_VAO_ALLOC
		mesh_data->individual_vao = new VAO(mesh_data->vbo_handle.vbo, mesh_data->indices_vbo_handle.index_vbo, mesh_data->vertex_spec);
#endif

		mesh_data->vert_data.clearAndFreeMem();
		mesh_data->vert_index_buffer.clearAndFreeMem();
		mesh_data->vert_index_buffer_uint16.clearAndFreeMem();
		mesh_data->vert_index_buffer_uint8.clearAndFreeMem();
	}

	//conPrint("ModelLoading::makeModelForVoxelGroup took " + timer.elapsedString());
	return mesh_data;
}


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <maths/PCG32.h>
#include <utils/TestUtils.h>


void ModelLoading::test()
{
	conPrint("ModelLoading::test()");

	glare::TaskManager task_manager;

	// Test two adjacent voxels with different materials.  All faces should be added.
	//{
	//	VoxelGroup group;
	//	group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(10, 0, 1), 1));
	//	group.voxels.push_back(Voxel(Vec3<int>(20, 0, 1), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(30, 0, 1), 1));
	//	group.voxels.push_back(Voxel(Vec3<int>(40, 0, 1), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(50, 0, 1), 1));

	//	Reference<RayMesh> raymesh;
	//	Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

	//	testAssert(data->batches.size() == 2);
	//	testAssert(raymesh->getTriangles().size() == 6 * 6 * 2);
	//}

	
	// Test a single voxel
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		testAssert(raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		testAssert(raymesh->getTriangles().size() == 6 * 2);
	}

	// Test two adjacent voxels with same material.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		testAssert(raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		testAssert(raymesh->getTriangles().size() == 6 * 2);
	}

	// Test two adjacent voxels (along y axis) with same material.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 1, 0), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		testAssert(raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		testAssert(raymesh->getNumVerts() == 8);
		testAssert(raymesh->getTriangles().size() == 2 * 6);
	}

	// Test two adjacent voxels (along z axis) with same material.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		testAssert(raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		testAssert(raymesh->getTriangles().size() == 2 * 6);
	}

	// Test two adjacent voxels with different materials.  All faces should be added.
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 1));

		Reference<RayMesh> raymesh;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

		testEqual(data->getNumVerts(), (size_t)(2 * 4 + 8 * 4));
		testAssert(raymesh->getNumVerts() == 4 * 3);
		testEqual(data->getNumTris(), (size_t)(2 * 6 * 2));
		testAssert(raymesh->getTriangles().size() == 2 * 6 * 2);
	}

	// Performance test
	if(false)
	{
		PCG32 rng(1);
		VoxelGroup group;
		for(int z=0; z<100; z += 2)
			for(int y=0; y<100; ++y)
				for(int x=0; x<20; ++x)
					if(rng.unitRandom() < 0.2f)
						group.voxels.push_back(Voxel(Vec3<int>(x, y, z), 0));

		for(int i=0; i<500; ++i)
		{
			Timer timer;

			Reference<RayMesh> raymesh;
			Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, raymesh);

			conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
			conPrint("Resulting num tris: " + toString(raymesh->getTriangles().size()));
		}
	}

	conPrint("ModelLoading::test() done.");
}


#endif // BUILD_TESTS
