/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "../shared/ResourceManager.h"
#include "../dll/include/IndigoMesh.h"
#include "../graphics/formatdecoderobj.h"
#include "../simpleraytracer/RayMesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StandardPrintOutput.h"


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	if(mat.colour->type == SpectrumVal::SpectrumValType_Constant)
	{
		opengl_mat.albedo_rgb = mat.colour.downcastToPtr<ConstantSpectrumVal>()->rgb;
		opengl_mat.albedo_tex_path = "";
	}
	else if(mat.colour->type == SpectrumVal::SpectrumValType_Texture)
	{
		opengl_mat.albedo_rgb = Colour3f(0.5f);
		opengl_mat.albedo_tex_path = resource_manager.pathForURL(mat.colour.downcastToPtr<TextureSpectrumVal>()->texture_url);
	}

	if(mat.roughness->type == ScalarVal::ScalarValType_Constant)
	{
		opengl_mat.roughness = mat.roughness.downcastToPtr<ConstantScalarVal>()->val;
	}
	else
	{
		opengl_mat.roughness = 0.5f;
	}

	if(mat.opacity->type == ScalarVal::ScalarValType_Constant)
	{
		opengl_mat.transparent = mat.opacity.downcastToPtr<ConstantScalarVal>()->val < 1.0f;
	}
	else
	{
		opengl_mat.transparent = false;
	}
}


static void checkValidAndSanitiseMesh(Indigo::Mesh& mesh)
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


// We don't have a material file, just the model file:
GLObjectRef ModelLoading::makeGLObjectForModelFile(const std::string& model_path, 
												   const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, float& suggested_scale_out, std::vector<WorldMaterialRef>& loaded_materials_out)
{
	if(hasExtension(model_path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/true, mats);

		checkValidAndSanitiseMesh(*mesh);

		// Convert model coordinates to z up
		js::AABBox aabb = js::AABBox::emptyAABBox();
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
		{
			mesh->vert_positions[i] = Indigo::Vec3f(mesh->vert_positions[i].x, -mesh->vert_positions[i].z, mesh->vert_positions[i].y);
			aabb.enlargeToHoldPoint(Vec4f(mesh->vert_positions[i].x, mesh->vert_positions[i].y, mesh->vert_positions[i].z, 1.f));
		}

		for(size_t i=0; i<mesh->vert_normals.size(); ++i)
			mesh->vert_normals[i] = Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y);

		// Automatically scale object down until it is < x m across
		const float max_span = 5.0f;
		suggested_scale_out = 1.f;
		float use_scale = 1.f;
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
			for(size_t i=0; i<mesh->vert_positions.size(); ++i)
				mesh->vert_positions[i] *= use_scale;
		}

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
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

					if(tex_path == "")
						loaded_materials_out[i]->colour = new ConstantSpectrumVal(mats.materials[z].Kd);
					else
						loaded_materials_out[i]->colour = new TextureSpectrumVal(tex_path);
					loaded_materials_out[i]->opacity = new ConstantScalarVal(ob->materials[i].alpha);
					loaded_materials_out[i]->roughness = new ConstantScalarVal(0.5f);

					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].roughness = 0.5f;

				loaded_materials_out[i]->colour = new TextureSpectrumVal("obstacle.png");
				loaded_materials_out[i]->opacity = new ConstantScalarVal(1.f);
				loaded_materials_out[i]->roughness = new ConstantScalarVal(0.5f);
			}

			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = mesh;
		return ob;
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
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].roughness = 0.5f;
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);

				loaded_materials_out[i] = new WorldMaterial();
				loaded_materials_out[i]->colour = new TextureSpectrumVal("obstacle.png");
				loaded_materials_out[i]->opacity = new ConstantScalarVal(1.f);
				loaded_materials_out[i]->roughness = new ConstantScalarVal(0.5f);
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
		options.bih_tri_threshold = 0;
		options.cache_trees = false;
		StandardPrintOutput print_output;
		raymesh->build(".", options, print_output, false, task_manager);

		raymesh->buildJSTris();

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
			ob->materials[i].albedo_tex_path = "obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}

		ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
	}

	mesh_out = mesh;
	raymesh_out = raymesh;
	return ob;
}


