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
#include "../graphics/formatdecoderobj.h"
#include "../graphics/FormatDecoderSTL.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../simpleraytracer/raymesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StandardPrintOutput.h"


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.albedo_tex_path = (mat.colour_texture_url.empty() ? "" : resource_manager.pathForURL(mat.colour_texture_url));

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = mat.opacity.val < 1.0f;

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	opengl_mat.tex_matrix = mat.tex_matrix;
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

	if(use_scale != 1.f)
	{
		conPrint("Scaling object by " + toString(use_scale));
		for(size_t i=0; i<mesh.vert_positions.size(); ++i)
			mesh.vert_positions[i] *= use_scale;
	}
}

// We don't have a material file, just the model file:
GLObjectRef ModelLoading::makeGLObjectForModelFile(const std::string& model_path, 
												   const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, std::vector<WorldMaterialRef>& loaded_materials_out)
{
	if(hasExtension(model_path, "obj"))
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
		const Matrix4f use_matrix = Matrix4f::translationMatrix(0, 0, -min_z) * ob_to_world_matrix;

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = use_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

		ob->materials.resize(mesh->num_materials_referenced);
		loaded_materials_out.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			loaded_materials_out[i] = new WorldMaterial();

			// Have we parsed such a material from the .mtl file?
			bool found_mat = false;
			for(size_t z=0; z<mats.materials.size(); ++z)
				if(mats.materials[z].name == toStdString(mesh->used_materials[i]))
				{
					const std::string tex_path = (!mats.materials[z].map_Kd.path.empty()) ? FileUtils::join(FileUtils::getDirectory(mats.mtl_file_path), mats.materials[z].map_Kd.path) : "";

					ob->materials[i].albedo_rgb = mats.materials[z].Kd;
					ob->materials[i].albedo_tex_path = tex_path;
					ob->materials[i].roughness = 0.5f;//mats.materials[z].Ns_exponent; // TODO: convert
					ob->materials[i].alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);

					loaded_materials_out[i]->colour_rgb = mats.materials[z].Kd;
					loaded_materials_out[i]->colour_texture_url = tex_path;
					loaded_materials_out[i]->opacity = ScalarVal(ob->materials[i].alpha);
					loaded_materials_out[i]->roughness = ScalarVal(0.5f);

					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				//ob->materials[i].albedo_tex_path = "resources/obstacle.png";
				ob->materials[i].roughness = 0.5f;

				//loaded_materials_out[i]->colour_texture_url = "resources/obstacle.png";
				loaded_materials_out[i]->opacity = ScalarVal(1.f);
				loaded_materials_out[i]->roughness = ScalarVal(0.5f);
			}

			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = mesh;
		return ob;
	}
	else if(hasExtension(model_path, "gltf"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		Timer timer;
		GLTFMaterials mats;
		FormatDecoderGLTF::streamModel(model_path, *mesh, 1.0f, mats);
		conPrint("Loaded GLTF model in " + timer.elapsedString());

		checkValidAndSanitiseMesh(*mesh);

		// Convert model coordinates to z up
		//for(size_t i=0; i<mesh->vert_positions.size(); ++i)
		//	mesh->vert_positions[i] = Indigo::Vec3f(mesh->vert_positions[i].x, -mesh->vert_positions[i].z, mesh->vert_positions[i].y);
		//
		//for(size_t i=0; i<mesh->vert_normals.size(); ++i)
		//	mesh->vert_normals[i] = Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
		timer.reset();
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);
		conPrint("Build OpenGL mesh for GLTF model in " + timer.elapsedString());

		if(mats.materials.size() < mesh->num_materials_referenced)
			throw Indigo::Exception("mats.materials had incorrect size.");

		ob->materials.resize(mesh->num_materials_referenced);
		loaded_materials_out.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<mesh->num_materials_referenced; ++i)
		{
			loaded_materials_out[i] = new WorldMaterial();

			const std::string tex_path = mats.materials[i].diffuse_map.path;

			ob->materials[i].albedo_rgb = mats.materials[i].diffuse;
			ob->materials[i].albedo_tex_path = tex_path;
			ob->materials[i].roughness = mats.materials[i].roughness;
			ob->materials[i].alpha = mats.materials[i].alpha;
			ob->materials[i].transparent = mats.materials[i].alpha < 1.0f;
			ob->materials[i].metallic_frac = mats.materials[i].metallic;

			loaded_materials_out[i]->colour_rgb = mats.materials[i].diffuse;
			loaded_materials_out[i]->colour_texture_url = tex_path;
			loaded_materials_out[i]->opacity = ScalarVal(ob->materials[i].alpha);
			loaded_materials_out[i]->roughness = mats.materials[i].roughness;
			loaded_materials_out[i]->opacity = mats.materials[i].alpha;

			ob->materials[i].tex_matrix = Matrix2f::identity();// Matrix2f(1, 0, 0, -1);
		}
		mesh_out = mesh;
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
			const Matrix4f use_matrix = Matrix4f::translationMatrix(0, 0, -min_z) * ob_to_world_matrix;

			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = use_matrix;
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

			ob->materials.resize(mesh->num_materials_referenced);
			loaded_materials_out.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

				loaded_materials_out[i] = new WorldMaterial();
			}

			mesh_out = mesh;
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
			
			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = ob_to_world_matrix;
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

			ob->materials.resize(mesh->num_materials_referenced);
			loaded_materials_out.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				//ob->materials[i].albedo_tex_path = "resources/obstacle.png";
				ob->materials[i].roughness = 0.5f;
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

				loaded_materials_out[i] = new WorldMaterial();
				//loaded_materials_out[i]->colour_texture_url = "resources/obstacle.png";
				loaded_materials_out[i]->opacity = ScalarVal(1.f);
				loaded_materials_out[i]->roughness = ScalarVal(0.5f);
			}
			
			mesh_out = mesh;
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


GLObjectRef ModelLoading::makeGLObjectForModelURLAndMaterials(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials,
												   ResourceManager& resource_manager, MeshManager& mesh_manager, Indigo::TaskManager& task_manager,
												   const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, Reference<RayMesh>& raymesh_out)
{
	// Load Indigo mesh and OpenGL mesh data, or get from mesh_manager if already loaded.
	Indigo::MeshRef mesh;
	Reference<OpenGLMeshRenderData> gl_meshdata;
	Reference<RayMesh> raymesh;

	if(mesh_manager.model_URL_to_mesh_map.count(model_URL) > 0)
	{
		mesh        = mesh_manager.model_URL_to_mesh_map[model_URL].mesh;
		gl_meshdata = mesh_manager.model_URL_to_mesh_map[model_URL].gl_meshdata;
		raymesh     = mesh_manager.model_URL_to_mesh_map[model_URL].raymesh;
	}
	else
	{
		// Load mesh from disk:
		const std::string model_path = resource_manager.pathForURL(model_URL);
		
		mesh = new Indigo::Mesh();

		if(hasExtension(model_path, "obj"))
		{
			MLTLibMaterials mats;
			FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws Indigo::Exception on failure.
		}
		else if(hasExtension(model_path, "stl"))
		{
			FormatDecoderSTL::streamModel(model_path, *mesh, 1.f);
		}
		else if(hasExtension(model_path, "gltf"))
		{
			GLTFMaterials mats;
			FormatDecoderGLTF::streamModel(model_path, *mesh, 1.0f, mats);
		}
		else if(hasExtension(model_path, "igmesh"))
		{
			try
			{
				Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);
			}
			catch(Indigo::IndigoException& e)
			{
				throw Indigo::Exception(toStdString(e.what()));
			}
		}
		else
			throw Indigo::Exception("unhandled model format: " + model_path);

		checkValidAndSanitiseMesh(*mesh); // Throws Indigo::Exception on invalid mesh.

		gl_meshdata = OpenGLEngine::buildIndigoMesh(mesh, /*skip opengl calls=*/false);


		raymesh = new RayMesh("mesh", false);
		raymesh->fromIndigoMesh(*mesh);

		raymesh->buildTrisFromQuads();
		Geometry::BuildOptions options;
		StandardPrintOutput print_output;
		raymesh->build(options, print_output, false, task_manager);

		// Add to map
		MeshData mesh_data;
		mesh_data.mesh = mesh;
		mesh_data.gl_meshdata = gl_meshdata;
		mesh_data.raymesh = raymesh;
		mesh_manager.model_URL_to_mesh_map[model_URL] = mesh_data;
	}

	// Make the GLObject
	GLObjectRef ob = new GLObject();
	ob->ob_to_world_matrix = ob_to_world_matrix;
	ob->mesh_data = gl_meshdata;

	ob->materials.resize(mesh->num_materials_referenced);
	for(uint32 i=0; i<ob->materials.size(); ++i)
	{
		if(i < materials.size())
		{
			setGLMaterialFromWorldMaterial(*materials[i], resource_manager, ob->materials[i]);
		}
		else
		{
			// Assign dummy mat
			ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			ob->materials[i].albedo_tex_path = "resources/obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}
	}

	mesh_out = mesh;
	raymesh_out = raymesh;
	return ob;
}


Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, Reference<RayMesh>& raymesh_out)
{
	const size_t num_voxels = voxel_group.voxels.size();
	// conPrint("Adding " + toString(num_voxels) + " voxels.");

	const float w = 1.f;// voxel_group.voxel_width;

	js::Vector<Vec3f, 16> verts;
	verts.resize(num_voxels * 24); // 6 faces * 4 verts/face

	js::Vector<Vec3f, 16> normals;
	normals.resize(num_voxels * 24);

	js::Vector<Vec2f, 16> uvs;
	uvs.resize(num_voxels * 24);

	js::Vector<uint32, 16> indices;
	indices.resize(num_voxels * 36); // two tris per face, 6 faces

	int max_mat_index = 0;
	for(int v=0; v<(int)num_voxels; ++v)
	{
		max_mat_index = myMax(max_mat_index, voxel_group.voxels[v].mat_index);

		Vec3f voxel_pos_offset(
			voxel_group.voxels[v].pos.x * w,
			voxel_group.voxels[v].pos.y * w,
			voxel_group.voxels[v].pos.z * w
		);

		for(int face = 0; face < 6; ++face)
		{
			indices[v*36 + face*6 + 0] = v*24 + face*4 + 0;
			indices[v*36 + face*6 + 1] = v*24 + face*4 + 1;
			indices[v*36 + face*6 + 2] = v*24 + face*4 + 2;
			indices[v*36 + face*6 + 3] = v*24 + face*4 + 0;
			indices[v*36 + face*6 + 4] = v*24 + face*4 + 2;
			indices[v*36 + face*6 + 5] = v*24 + face*4 + 3;
		}

		int face = 0;

		// x = 0 face
		{
			Vec3f v0 = Vec3f(0, 0, 0) + voxel_pos_offset;
			Vec3f v1 = Vec3f(0, 0, w) + voxel_pos_offset;
			Vec3f v2 = Vec3f(0, w, w) + voxel_pos_offset;
			Vec3f v3 = Vec3f(0, w, 0) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 2] = Vec2f(0, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 0);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(-1, 0, 0);

			face++;
		}

		// x = 1 face
		{
			Vec3f v0 = Vec3f(w, 0, 0) + voxel_pos_offset;
			Vec3f v1 = Vec3f(w, w, 0) + voxel_pos_offset;
			Vec3f v2 = Vec3f(w, w, w) + voxel_pos_offset;
			Vec3f v3 = Vec3f(w, 0, w) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(0, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 2] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(1, 0, 0);

			face++;
		}

		// y = 0 face
		{
			Vec3f v0 = Vec3f(0, 0, 0) + voxel_pos_offset;
			Vec3f v1 = Vec3f(w, 0, 0) + voxel_pos_offset;
			Vec3f v2 = Vec3f(w, 0, w) + voxel_pos_offset;
			Vec3f v3 = Vec3f(0, 0, w) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(0, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 2] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(0, -1, 0);

			face++;
		}

		// y = 1 face
		{
			Vec3f v0 = Vec3f(0, w, 0) + voxel_pos_offset;
			Vec3f v1 = Vec3f(0, w, w) + voxel_pos_offset;
			Vec3f v2 = Vec3f(w, w, w) + voxel_pos_offset;
			Vec3f v3 = Vec3f(w, w, 0) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 2] = Vec2f(0, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 0);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(0, 1, 0);

			face++;
		}

		// z = 0 face
		{
			Vec3f v0 = Vec3f(0, 0, 0) + voxel_pos_offset;
			Vec3f v1 = Vec3f(0, w, 0) + voxel_pos_offset;
			Vec3f v2 = Vec3f(w, w, 0) + voxel_pos_offset;
			Vec3f v3 = Vec3f(w, 0, 0) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(0, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 2] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(0, 0, -1);

			face++;
		}

		// z = 1 face
		{
			Vec3f v0 = Vec3f(0, 0, w) + voxel_pos_offset;
			Vec3f v1 = Vec3f(w, 0, w) + voxel_pos_offset;
			Vec3f v2 = Vec3f(w, w, w) + voxel_pos_offset;
			Vec3f v3 = Vec3f(0, w, w) + voxel_pos_offset;

			verts[v*24 + face*4 + 0] = v0;
			verts[v*24 + face*4 + 1] = v1;
			verts[v*24 + face*4 + 2] = v2;
			verts[v*24 + face*4 + 3] = v3;

			uvs[v*24 + face*4 + 0] = Vec2f(0, 0);
			uvs[v*24 + face*4 + 1] = Vec2f(1, 0);
			uvs[v*24 + face*4 + 2] = Vec2f(1, 1);
			uvs[v*24 + face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[v*24 + face*4 + i] = Vec3f(0, 0, 1);

			face++;
		}
	}


	Reference<RayMesh> raymesh = new RayMesh("mesh", false);

	// Copy over tris to raymesh
	raymesh->getTriangles().resizeNoCopy(num_voxels*6*2);
	for(size_t v=0; v<num_voxels; ++v)
	{
		for(int face=0; face<6; ++face)
		{
			raymesh->getTriangles()[v*6*2 + face*2 + 0].vertex_indices[0] = indices[v*36 + face*6 + 0];
			raymesh->getTriangles()[v*6*2 + face*2 + 0].vertex_indices[1] = indices[v*36 + face*6 + 1];
			raymesh->getTriangles()[v*6*2 + face*2 + 0].vertex_indices[2] = indices[v*36 + face*6 + 2];
			raymesh->getTriangles()[v*6*2 + face*2 + 0].setMatIndexAndUseShadingNormals(voxel_group.voxels[v].mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);

			raymesh->getTriangles()[v*6*2 + face*2 + 1].vertex_indices[0] = indices[v*36 + face*6 + 3];
			raymesh->getTriangles()[v*6*2 + face*2 + 1].vertex_indices[1] = indices[v*36 + face*6 + 4];
			raymesh->getTriangles()[v*6*2 + face*2 + 1].vertex_indices[2] = indices[v*36 + face*6 + 5];
			raymesh->getTriangles()[v*6*2 + face*2 + 1].setMatIndexAndUseShadingNormals(voxel_group.voxels[v].mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);
		}
	}

	// Copy verts positions and normals
	raymesh->getVertices().resizeNoCopy(num_voxels*24);
	for(size_t i=0; i<num_voxels*24; ++i)
	{
		raymesh->getVertices()[i].pos = verts[i];
		raymesh->getVertices()[i].normal = normals[i];
	}


	//raymesh->buildTrisFromQuads();
	Geometry::BuildOptions options;
	StandardPrintOutput print_output;
	raymesh->build(options, print_output, /*verbose=*/false, task_manager);

	raymesh_out = raymesh;

	Reference<OpenGLMeshRenderData> meshdata = new OpenGLMeshRenderData();

	// Do a pass over voxels to count number of voxels for each mat
	const int num_mat_batches = max_mat_index + 1;
	std::vector<int> mat_voxel_counts(num_mat_batches);
	for(size_t v=0; v<num_voxels; ++v)
		mat_voxel_counts[voxel_group.voxels[v].mat_index]++;

	meshdata->has_uvs = true;
	meshdata->has_shading_normals = true;
	meshdata->batches.resize(num_mat_batches);
	int batch_offset = 0; // in voxels
	std::vector<int> batch_write_indices(num_mat_batches); // in voxels
	for(size_t i=0; i<num_mat_batches; ++i)
	{
		meshdata->batches[i].material_index = (uint32)i;
		meshdata->batches[i].num_indices = mat_voxel_counts[i] * 36; // 36 indices per voxel.
		meshdata->batches[i].prim_start_offset = batch_offset * 36 * sizeof(uint32);

		batch_write_indices[i] = batch_offset;
		batch_offset += mat_voxel_counts[i];
	}

	meshdata->aabb_os = js::AABBox::emptyAABBox();

	js::Vector<float, 16> combined_data;
	const int NUM_COMPONENTS = 8;
	combined_data.resizeNoCopy(NUM_COMPONENTS * verts.size());

	js::Vector<uint32, 16> sorted_indices(indices.size());

	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int mat_index = voxel_group.voxels[v].mat_index;
		const int write_i = batch_write_indices[mat_index]++;

		// Copy the vert data etc. for the voxel
		// There are 24 verts, normals etc.. per voxel.
		const int verts_offset = v * 24;
		for(size_t i=0; i<24; ++i)
		{
			const Vec3f& vertpos = verts[verts_offset + i];

			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 0] = vertpos.x;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 1] = vertpos.y;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 2] = vertpos.z;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 3] = normals[verts_offset + i].x;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 4] = normals[verts_offset + i].y;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 5] = normals[verts_offset + i].z;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 6] = uvs[verts_offset + i].x;
			combined_data[(write_i*24 + i)*NUM_COMPONENTS + 7] = uvs[verts_offset + i].y;

			meshdata->aabb_os.enlargeToHoldPoint(Vec4f(vertpos.x, vertpos.y, vertpos.z, 1.f));
		}

		// Copy the indices for the voxel.
		// There are 36 indices for the voxel
		for(size_t i=0; i<36; ++i)
			sorted_indices[write_i * 36 + i] = indices[v*36 + i];
	}

	meshdata->vert_vbo = new VBO(&combined_data[0], combined_data.dataSizeBytes());
	meshdata->vert_indices_buf = new VBO(&sorted_indices[0], sorted_indices.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
	meshdata->index_type = GL_UNSIGNED_INT;

	VertexSpec spec;
	const uint32 vert_stride = (uint32)(sizeof(float) * 3 + sizeof(float) * 3 + sizeof(float) * 2); // also vertex size.

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = vert_stride;
	pos_attrib.offset = 0;
	spec.attributes.push_back(pos_attrib);

	VertexAttrib normal_attrib;
	normal_attrib.enabled = true;
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
	uv_attrib.offset = (uint32)(sizeof(float) * 3 + sizeof(float) * 3); // after position and possibly normal.
	spec.attributes.push_back(uv_attrib);

	meshdata->vert_vao = new VAO(meshdata->vert_vbo, spec);

	return meshdata;
}
