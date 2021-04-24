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
#include "../utils/IncludeHalf.h"
#include "../indigo/UVUnwrapper.h"
#include <limits>


bool MeshManager::isMeshDataInserted(const std::string& model_url) const
{
	Lock lock(mutex);

	return model_URL_to_mesh_map.count(model_url) > 0;
}


GLMemUsage MeshManager::getTotalMemUsage() const
{
	Lock lock(mutex);

	GLMemUsage sum;
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
	{
		sum += it->second.gl_meshdata->getTotalMemUsage();
	}
	return sum;
}


//size_t MeshManager::getTotalGPUMemUsage() const
//{
//	Lock lock(mutex);
//
//	size_t sum = 0;
//	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
//	{
//		sum += it->second.gl_meshdata->getTotalGPUMemUsage();
//	}
//	return sum;
//}


void ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.tex_path = mat.colour_texture_url;

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	// glTexImage2D expects the start of the texture data to be the lower left of the image, whereas it is actually the upper left.  So flip y coord to compensate.
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
		const VoxModel& model = vox_contents.models[0];
		loaded_object_out.getDecompressedVoxels().resize(model.voxels.size());
		for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
		{
			loaded_object_out.getDecompressedVoxels()[i].pos = Vec3<int>(model.voxels[i].x, model.voxels[i].y, model.voxels[i].z);
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
		const float use_scale = 0.1f;

		// Make opengl object
		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = Matrix4f::uniformScaleMatrix(use_scale);

		Reference<RayMesh> raymesh;
		ob->mesh_data = ModelLoading::makeModelForVoxelGroup(loaded_object_out.getDecompressedVoxelGroup(), task_manager, /*do opengl stuff=*/true, raymesh);

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

		// Now that vertices have been modified, recompute AABB
		mesh->endOfModel();

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
		timer.reset();
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);
		conPrint("Build OpenGL mesh for GLTF model in " + timer.elapsedString());

		if(mats.materials.size() < mesh->num_materials_referenced)
			throw glare::Exception("mats.materials had incorrect size.");

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
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, /*skip_opengl_calls=*/false);

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
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));
}


GLObjectRef ModelLoading::makeGLObjectForModelURLAndMaterials(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials, const std::string& lightmap_url,
												   ResourceManager& resource_manager, MeshManager& mesh_manager, glare::TaskManager& task_manager,
												   const Matrix4f& ob_to_world_matrix, bool skip_opengl_calls, Reference<RayMesh>& raymesh_out)
{
	// Load Indigo mesh and OpenGL mesh data, or get from mesh_manager if already loaded.
	size_t num_materials_referenced;
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
		num_materials_referenced = mesh_manager.model_URL_to_mesh_map[model_URL].num_materials_referenced;
		gl_meshdata  = mesh_manager.model_URL_to_mesh_map[model_URL].gl_meshdata;
		raymesh      = mesh_manager.model_URL_to_mesh_map[model_URL].raymesh;
	}
	else
	{
		// Load mesh from disk:
		const std::string model_path = resource_manager.pathForURL(model_URL);
		
		BatchedMeshRef batched_mesh = new BatchedMesh();

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

		gl_meshdata = OpenGLEngine::buildBatchedMesh(batched_mesh, /*skip opengl calls=*/skip_opengl_calls);

		// Build RayMesh from our batched mesh (used for physics + picking)
		raymesh = new RayMesh("mesh", false);
		raymesh->fromBatchedMesh(*batched_mesh);

		Geometry::BuildOptions options;
		DummyShouldCancelCallback should_cancel_callback;
		StandardPrintOutput print_output;
		raymesh->build(options, should_cancel_callback, print_output, false, task_manager);

		num_materials_referenced = batched_mesh->numMaterialsReferenced();

		// Add to map
		MeshData mesh_data;
		mesh_data.num_materials_referenced = num_materials_referenced;
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

	ob->materials.resize(num_materials_referenced);
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


Reference<Indigo::Mesh> ModelLoading::makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group)
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

	return doMakeIndigoMeshForVoxelGroup(voxels, num_mats, voxel_hash);
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


static Reference<OpenGLMeshRenderData> buildVoxelOpenGLMeshData(const Indigo::Mesh& mesh_, const Vec3<int>& minpos, const Vec3<int>& maxpos)
{
	const int min_voxel_coord = myMin(minpos.x, minpos.y, minpos.z);
	const int max_voxel_coord = myMax(maxpos.x, maxpos.y, maxpos.z) + 1;

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


	if(num_uv_sets >= 1)
	{
		VertexAttrib uv_attrib;
		uv_attrib.enabled = true;
		uv_attrib.num_comps = 2;
		uv_attrib.type = GL_HALF_FLOAT;
		uv_attrib.normalised = false;
		uv_attrib.stride = (uint32)num_bytes_per_vert;
		uv_attrib.offset = (uint32)uv0_offset;
		mesh_data->vertex_spec.attributes.push_back(uv_attrib);
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
		Vec4f((float)minpos.x,       (float)minpos.y,       (float)minpos.z,       1.f),
		Vec4f((float)(maxpos.x + 1), (float)(maxpos.y + 1), (float)(maxpos.z + 1), 1.f) // Add 1 to take into account extent of voxels.
	);

	return mesh_data;
}


Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, glare::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out)
{
	// TEMP NEW:
	//Timer timer;

	// Iterate over voxels and get voxel position bounds
	Vec3<int> minpos( 1000000000);
	Vec3<int> maxpos(-1000000000);
	for(size_t i=0; i<voxel_group.voxels.size(); ++i)
	{
		minpos = minpos.min(voxel_group.voxels[i].pos);
		maxpos = maxpos.max(voxel_group.voxels[i].pos);
	}

	Indigo::MeshRef indigo_mesh = makeIndigoMeshForVoxelGroup(voxel_group);

	// UV unwrap it:
	StandardPrintOutput print_output;
	
	const float normed_margin = 2.f / 1024; // NOTE: we don't know what res lightmap we will be using here.
	UVUnwrapper::build(*indigo_mesh, print_output, normed_margin); // Adds UV set to indigo_mesh.


	//----------------- Convert indigo mesh to voxel data -------------
	Reference<OpenGLMeshRenderData> mesh_data = buildVoxelOpenGLMeshData(*indigo_mesh, minpos, maxpos);
	//----------------- End Convert indigo mesh to voxel data -------------


	// Build RayMesh from our batched mesh (used for physics + picking)
	raymesh_out = new RayMesh("mesh", /*enable_shading_normals=*/false);
	raymesh_out->fromIndigoMesh(*indigo_mesh);

	Geometry::BuildOptions options;
	DummyShouldCancelCallback should_cancel_callback;
	raymesh_out->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);

	// Load rendering data into GPU mem if requested.
	if(do_opengl_stuff)
	{
		if(!mesh_data->vert_index_buffer_uint8.empty())
		{
			mesh_data->vert_indices_buf = new VBO(mesh_data->vert_index_buffer_uint8.data(), mesh_data->vert_index_buffer_uint8.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
			assert(mesh_data->index_type == GL_UNSIGNED_BYTE);
		}
		else if(!mesh_data->vert_index_buffer_uint16.empty())
		{
			mesh_data->vert_indices_buf = new VBO(mesh_data->vert_index_buffer_uint16.data(), mesh_data->vert_index_buffer_uint16.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
			assert(mesh_data->index_type == GL_UNSIGNED_SHORT);
		}
		else
		{
			mesh_data->vert_indices_buf = new VBO(mesh_data->vert_index_buffer.data(), mesh_data->vert_index_buffer.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
			assert(mesh_data->index_type == GL_UNSIGNED_INT);
		}

		mesh_data->vert_vbo = new VBO(mesh_data->vert_data.data(), mesh_data->vert_data.dataSizeBytes());
		mesh_data->vert_vao = new VAO(mesh_data->vert_vbo, mesh_data->vertex_spec);

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
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

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
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

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
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

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
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

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
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

		testEqual(data->getNumVerts(), (size_t)(2 * 4 + 8 * 4));
		testAssert(raymesh->getNumVerts() == 4 * 3);
		testEqual(data->getNumTris(), (size_t)(2 * 6 * 2));
		testAssert(raymesh->getTriangles().size() == 2 * 6 * 2);
	}

	// Performance test
	if(true)
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
			Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

			conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
			conPrint("Resulting num tris: " + toString(raymesh->getTriangles().size()));
		}
	}
}


#endif
