/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
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
#include <limits>


bool MeshManager::isMeshDataInserted(const std::string& model_url) const
{
	Lock lock(mutex);

	return model_URL_to_mesh_map.count(model_url) > 0;
}


void ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.tex_path = mat.colour_texture_url;

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;
}


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, const std::string& lightmap_url, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.tex_path = (mat.colour_texture_url.empty() ? "" : resource_manager.pathForURL(mat.colour_texture_url));
	opengl_mat.lightmap_path = (lightmap_url.empty() ? "" : resource_manager.pathForURL(lightmap_url));

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;
}


void ModelLoading::checkValidAndSanitiseMesh(Indigo::Mesh& mesh)
{
	if(mesh.num_uv_mappings > 10)
		throw Indigo::Exception("Too many UV sets: " + toString(mesh.num_uv_mappings) + ", max is " + toString(10));

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
				throw Indigo::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(mesh.triangles[i].vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 3; ++v)
				if(src_tri.uv_indices[v] >= num_uv_groups)
					throw Indigo::Exception("Triangle uv index is out of bounds.  (uv index=" + toString(mesh.triangles[i].uv_indices[v]) + ")");
	}

	// Quads
	for(size_t i = 0; i < mesh.quads.size(); ++i)
	{
		// Check vertex indices are in bounds
		for(unsigned int v = 0; v < 4; ++v)
			if(mesh.quads[i].vertex_indices[v] >= num_verts)
				throw Indigo::Exception("Quad vertex index is out of bounds.  (vertex index=" + toString(mesh.quads[i].vertex_indices[v]) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 4; ++v)
				if(mesh.quads[i].uv_indices[v] >= num_uv_groups)
					throw Indigo::Exception("Quad uv index is out of bounds.  (uv index=" + toString(mesh.quads[i].uv_indices[v]) + ")");
	}
}


void ModelLoading::checkValidAndSanitiseMesh(BatchedMesh& mesh)
{
	if(mesh.numMaterialsReferenced() > 10000)
		throw Indigo::Exception("Too many materials referenced.");

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

		for(unsigned int v = 0; v < 3; ++v)
			if(vertex_indices[v] >= num_verts)
				throw Indigo::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");
	}
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

// We don't have a material file, just the model file:
GLObjectRef ModelLoading::makeGLObjectForModelFile(
	Indigo::TaskManager& task_manager, 
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
		const VoxModel& model = vox_contents.models[0];
		loaded_object_out.voxel_group.voxels.resize(model.voxels.size());
		for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
		{
			loaded_object_out.voxel_group.voxels[i].pos = Vec3<int>(model.voxels[i].x, model.voxels[i].y, model.voxels[i].z);
			loaded_object_out.voxel_group.voxels[i].mat_index = model.voxels[i].mat_index;
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
		const float use_scale = 0.1f;

		// Make opengl object
		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = Matrix4f::uniformScaleMatrix(use_scale);

		Reference<RayMesh> raymesh;
		ob->mesh_data = ModelLoading::makeModelForVoxelGroup(loaded_object_out.voxel_group, task_manager, /*do opengl stuff=*/true, raymesh);

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

		for(size_t i=0; i<mesh->vert_normals.size(); ++i)
			mesh->vert_normals[i] = Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y);

		// Automatically scale object down until it is < x m across
		scaleMesh(*mesh);

		// Get smallest z coord
		float min_z = std::numeric_limits<float>::max();
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			min_z = myMin(min_z, mesh->vert_positions[i].z);

		// Move object so that it lies on the z=0 (ground) plane
		const Matrix4f use_matrix = Matrix4f::identity(); // Matrix4f::translationMatrix(0, 0, -min_z)* ob_to_world_matrix;

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = use_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

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
	else if(hasExtension(model_path, "gltf") || hasExtension(model_path, "glb"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		Timer timer;
		GLTFMaterials mats;
		if(hasExtension(model_path, "gltf"))
			FormatDecoderGLTF::streamModel(model_path, *mesh, 1.0f, mats);
		else
			FormatDecoderGLTF::loadGLBFile(model_path, *mesh, 1.0f, mats);
		conPrint("Loaded GLTF model in " + timer.elapsedString());

		checkValidAndSanitiseMesh(*mesh);

		// Convert model coordinates to z up
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			mesh->vert_positions[i] = Indigo::Vec3f(mesh->vert_positions[i].x, -mesh->vert_positions[i].z, mesh->vert_positions[i].y);
		//
		for(size_t i=0; i<mesh->vert_normals.size(); ++i)
			mesh->vert_normals[i] = Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y);

		// Automatically scale object down until it is < x m across
		scaleMesh(*mesh);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
		timer.reset();
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);
		conPrint("Build OpenGL mesh for GLTF model in " + timer.elapsedString());

		if(mats.materials.size() < mesh->num_materials_referenced)
			throw Indigo::Exception("mats.materials had incorrect size.");

		ob->materials.resize(mesh->num_materials_referenced);
		loaded_object_out.materials.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<mesh->num_materials_referenced; ++i)
		{
			loaded_object_out.materials[i] = new WorldMaterial();

			const std::string tex_path = mats.materials[i].diffuse_map.path;

			ob->materials[i].albedo_rgb = mats.materials[i].diffuse;
			ob->materials[i].tex_path = tex_path;
			ob->materials[i].roughness = mats.materials[i].roughness;
			ob->materials[i].alpha = mats.materials[i].alpha;
			ob->materials[i].transparent = mats.materials[i].alpha < 1.0f;
			ob->materials[i].metallic_frac = mats.materials[i].metallic;
			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

			loaded_object_out.materials[i]->colour_rgb = mats.materials[i].diffuse;
			loaded_object_out.materials[i]->colour_texture_url = tex_path;
			loaded_object_out.materials[i]->opacity = ScalarVal(ob->materials[i].alpha);
			loaded_object_out.materials[i]->roughness = mats.materials[i].roughness;
			loaded_object_out.materials[i]->opacity = mats.materials[i].alpha;
			loaded_object_out.materials[i]->metallic_fraction = mats.materials[i].metallic;
			loaded_object_out.materials[i]->tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = new BatchedMesh();
		mesh_out->buildFromIndigoMesh(*mesh);
		return ob;
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

			// Get smallest z coord
			float min_z = std::numeric_limits<float>::max();
			for(size_t i=0; i<mesh->vert_positions.size(); ++i)
				min_z = myMin(min_z, mesh->vert_positions[i].z);

			// Move object so that it lies on the z=0 (ground) plane
			const Matrix4f use_matrix = Matrix4f::translationMatrix(0, 0, -min_z);// *ob_to_world_matrix;

			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = use_matrix;
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

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
			throw Indigo::Exception(toStdString(e.what()));
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
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

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
			throw Indigo::Exception(toStdString(e.what()));
		}
	}
	else
		throw Indigo::Exception("unhandled format: " + model_path);
}


GLObjectRef ModelLoading::makeGLObjectForModelURLAndMaterials(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials, const std::string& lightmap_url,
												   ResourceManager& resource_manager, MeshManager& mesh_manager, Indigo::TaskManager& task_manager,
												   const Matrix4f& ob_to_world_matrix, bool skip_opengl_calls, Reference<RayMesh>& raymesh_out)
{
	// Load Indigo mesh and OpenGL mesh data, or get from mesh_manager if already loaded.
	BatchedMeshRef batched_mesh;
	Reference<OpenGLMeshRenderData> gl_meshdata;
	Reference<RayMesh> raymesh;

	bool present;
	{
		Lock lock(mesh_manager.mutex);
		present = mesh_manager.model_URL_to_mesh_map.count(model_URL) > 0;
	}

	if(present)
	{
		Lock lock(mesh_manager.mutex);
		batched_mesh = mesh_manager.model_URL_to_mesh_map[model_URL].mesh;
		gl_meshdata  = mesh_manager.model_URL_to_mesh_map[model_URL].gl_meshdata;
		raymesh      = mesh_manager.model_URL_to_mesh_map[model_URL].raymesh;
	}
	else
	{
		// Load mesh from disk:
		const std::string model_path = resource_manager.pathForURL(model_URL);
		
		batched_mesh = new BatchedMesh();

		//if(hasExtension(model_path, "vox"))
		//{
		//	VoxFileContents vox_content;
		//	FormatDecoderVox::loadModel(model_path, vox_content);

		//	// Convert voxels
		//	const VoxModel& model = vox_contents.models[0];
		//	loaded_object_out.voxel_group.voxels.resize(model.voxels.size());
		//	for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
		//	{
		//		loaded_object_out.voxel_group.voxels[i].pos = Vec3<int>(model.voxels[i].x, model.voxels[i].y, model.voxels[i].z);
		//		loaded_object_out.voxel_group.voxels[i].mat_index = model.voxels[i].mat_index;
		//	}
		//}
		//else 
		if(hasExtension(model_path, "obj"))
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();

			MLTLibMaterials mats;
			FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws Indigo::Exception on failure.

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
			Indigo::MeshRef mesh = new Indigo::Mesh();

			GLTFMaterials mats;
			FormatDecoderGLTF::streamModel(model_path, *mesh, 1.0f, mats);

			batched_mesh->buildFromIndigoMesh(*mesh);
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
				throw Indigo::Exception(toStdString(e.what()));
			}

			batched_mesh->buildFromIndigoMesh(*mesh);
		}
		else if(hasExtension(model_path, "bmesh"))
		{
			BatchedMesh::readFromFile(model_path, *batched_mesh);
		}
		else
			throw Indigo::Exception("unhandled model format: " + model_path);


		checkValidAndSanitiseMesh(*batched_mesh); // Throws Indigo::Exception on invalid mesh.

		gl_meshdata = OpenGLEngine::buildBatchedMesh(batched_mesh, /*skip opengl calls=*/skip_opengl_calls);

		// Build RayMesh from our batched mesh (used for physics + picking)
		raymesh = new RayMesh("mesh", false);
		raymesh->fromBatchedMesh(*batched_mesh);

		Geometry::BuildOptions options;
		DummyShouldCancelCallback should_cancel_callback;
		StandardPrintOutput print_output;
		raymesh->build(options, should_cancel_callback, print_output, false, task_manager);

		// Add to map
		MeshData mesh_data;
		mesh_data.mesh = batched_mesh;
		mesh_data.gl_meshdata = gl_meshdata;
		mesh_data.raymesh = raymesh;
		{
			Lock lock(mesh_manager.mutex);
			mesh_manager.model_URL_to_mesh_map[model_URL] = mesh_data;
		}
	}

	// Make the GLObject
	GLObjectRef ob = new GLObject();
	ob->ob_to_world_matrix = ob_to_world_matrix;
	ob->mesh_data = gl_meshdata;

	ob->materials.resize(batched_mesh->numMaterialsReferenced());
	for(uint32 i=0; i<ob->materials.size(); ++i)
	{
		if(i < materials.size())
		{
			setGLMaterialFromWorldMaterial(*materials[i], lightmap_url, resource_manager, ob->materials[i]);
		}
		else
		{
			// Assign dummy mat
			ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			ob->materials[i].tex_path = "resources/obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}
	}

	raymesh_out = raymesh;
	return ob;
}


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


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <indigo/TestUtils.h>


void ModelLoading::test()
{
	Indigo::TaskManager task_manager;

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
}


#endif
