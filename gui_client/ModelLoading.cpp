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
#include "../graphics/FormatDecoderVox.h"
#include "../simpleraytracer/raymesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StandardPrintOutput.h"
#include "../utils/HashMapInsertOnly2.h"
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


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_rgb = mat.colour_rgb;
	opengl_mat.tex_path = (mat.colour_texture_url.empty() ? "" : resource_manager.pathForURL(mat.colour_texture_url));

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
GLObjectRef ModelLoading::makeGLObjectForModelFile(Indigo::TaskManager& task_manager, const std::string& model_path,
												   Indigo::MeshRef& mesh_out, 
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

			loaded_object_out.materials[i]->colour_rgb = mats.materials[i].diffuse;
			loaded_object_out.materials[i]->colour_texture_url = tex_path;
			loaded_object_out.materials[i]->opacity = ScalarVal(ob->materials[i].alpha);
			loaded_object_out.materials[i]->roughness = mats.materials[i].roughness;
			loaded_object_out.materials[i]->opacity = mats.materials[i].alpha;

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
												   const Matrix4f& ob_to_world_matrix, bool skip_opengl_calls, Indigo::MeshRef& mesh_out, Reference<RayMesh>& raymesh_out)
{
	// Load Indigo mesh and OpenGL mesh data, or get from mesh_manager if already loaded.
	Indigo::MeshRef mesh;
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
		mesh        = mesh_manager.model_URL_to_mesh_map[model_URL].mesh;
		gl_meshdata = mesh_manager.model_URL_to_mesh_map[model_URL].gl_meshdata;
		raymesh     = mesh_manager.model_URL_to_mesh_map[model_URL].raymesh;
	}
	else
	{
		// Load mesh from disk:
		const std::string model_path = resource_manager.pathForURL(model_URL);
		
		mesh = new Indigo::Mesh();

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

		gl_meshdata = OpenGLEngine::buildIndigoMesh(mesh, /*skip opengl calls=*/skip_opengl_calls);


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
		{
			Lock lock(mesh_manager.mutex);
			mesh_manager.model_URL_to_mesh_map[model_URL] = mesh_data;
		}
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
			ob->materials[i].tex_path = "resources/obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}
	}

	mesh_out = mesh;
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

struct VoxelBuildInfo
{
	int face_offset; // number of faces added before this voxel.
	int num_faces; // num faces added for this voxel.
};


/*
raymesh->getUVs()[0] = Vec2f(0, 0);
raymesh->getUVs()[1] = Vec2f(0, 1);
raymesh->getUVs()[2] = Vec2f(1, 1);
raymesh->getUVs()[3] = Vec2f(1, 0);
*/
static inline unsigned int getUVIndex(const Vec2f& uv)
{
	// TODO: can optimise by checking x first and branching, then y.
	if(uv == Vec2f(0, 0))
		return 0;
	else if(uv == Vec2f(0, 1))
		return 1;
	else if(uv == Vec2f(1, 1))
		return 2;
	else
		return 3;
}


Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out)
{
	const size_t num_voxels = voxel_group.voxels.size();
	assert(num_voxels > 0);
	// conPrint("Adding " + toString(num_voxels) + " voxels.");

	// hash from voxel indices to voxel material
	const Vec3<int> empty_key(std::numeric_limits<int>::max());
	HashMapInsertOnly2<Vec3<int>, int, VoxelHashFunc> voxel_hash(/*empty key=*/empty_key, /*expected_num_items=*/num_voxels);

	for(int v=0; v<(int)num_voxels; ++v)
		voxel_hash.insert(std::make_pair(voxel_group.voxels[v].pos, voxel_group.voxels[v].mat_index));

	const float w = 1.f;

	js::Vector<VoxelBuildInfo, 16> voxel_info(num_voxels);

	js::Vector<Vec3f, 16> verts;
	verts.resize(num_voxels * 24); // 6 faces * 4 verts/face

	js::Vector<Vec3f, 16> normals;
	normals.resize(num_voxels * 24);

	js::Vector<Vec2f, 16> uvs;
	uvs.resize(num_voxels * 24);

	// Each face has 2 tris, each of which has 3 indices, so 6 indices per face.
	js::Vector<uint32, 16> indices;
	indices.resize(num_voxels * 36); // 6 indices per face * 6 faces per voxel = 36 indices per voxel

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	int face = 0; // total face write index

	int max_mat_index = 0;
	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int voxel_mat_i = voxel_group.voxels[v].mat_index;
		max_mat_index = myMax(max_mat_index, voxel_mat_i);

		const Vec3<int> v_p = voxel_group.voxels[v].pos;

		const Vec3f voxel_pos_offset(v_p.x, v_p.y, v_p.z);

		const int initial_face = face;

		// x = 0 face
		auto res = voxel_hash.find(Vec3<int>(v_p.x - 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(0, 0, 0) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(0, 0, w) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(0, w, w) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(0, w, 0) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(1, 0);
			uvs[face*4 + 1] = Vec2f(1, 1);
			uvs[face*4 + 2] = Vec2f(0, 1);
			uvs[face*4 + 3] = Vec2f(0, 0);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(-1, 0, 0);

			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		// x = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x + 1, v_p.y, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(w, 0, 0) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(w, w, 0) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(w, w, w) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(w, 0, w) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(1, 0, 0);

			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		// y = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y - 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(0, 0, 0) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(w, 0, 0) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(w, 0, w) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(0, 0, w) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(0, -1, 0);

			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		// y = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y + 1, v_p.z));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(0, w, 0) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(0, w, w) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(w, w, w) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(w, w, 0) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(1, 0);
			uvs[face*4 + 1] = Vec2f(1, 1);
			uvs[face*4 + 2] = Vec2f(0, 1);
			uvs[face*4 + 3] = Vec2f(0, 0);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(0, 1, 0);
			
			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		// z = 0 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z - 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(0, 0, 0) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(0, w, 0) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(w, w, 0) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(w, 0, 0) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(0, 0, -1);

			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		// z = 1 face
		res = voxel_hash.find(Vec3<int>(v_p.x, v_p.y, v_p.z + 1));
		if((res == voxel_hash.end()) || (res->second != voxel_mat_i)) // If neighbouring voxel is empty, or has a different material:
		{
			verts[face*4 + 0] = Vec3f(0, 0, w) + voxel_pos_offset;
			verts[face*4 + 1] = Vec3f(w, 0, w) + voxel_pos_offset;
			verts[face*4 + 2] = Vec3f(w, w, w) + voxel_pos_offset;
			verts[face*4 + 3] = Vec3f(0, w, w) + voxel_pos_offset;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int i=0; i<4; ++i)
				normals[face*4 + i] = Vec3f(0, 0, 1);

			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;

			face++;
		}

		voxel_info[v].face_offset = initial_face;
		voxel_info[v].num_faces = face - initial_face;

		aabb_os.enlargeToHoldPoint(voxel_pos_offset.toVec4fPoint());
		aabb_os.enlargeToHoldPoint((voxel_pos_offset + Vec3f(1,1,1)).toVec4fPoint());
	}

	const size_t num_faces = face;

	Reference<RayMesh> raymesh = new RayMesh("mesh", false);

	// Copy over tris to raymesh
	raymesh->getTriangles().resizeNoCopy(num_faces * 2);
	RayMeshTriangle* const dest_tris = raymesh->getTriangles().data();
	for(size_t v=0; v<num_voxels; ++v)
	{
		const int mat_index = voxel_group.voxels[v].mat_index;
		const int face_offset = voxel_info[v].face_offset;
		const int voxel_num_faces = voxel_info[v].num_faces;

		for(int f=face_offset; f<face_offset + voxel_num_faces; ++f)
		{
			dest_tris[f*2 + 0].vertex_indices[0] = indices[f*6 + 0];
			dest_tris[f*2 + 0].vertex_indices[1] = indices[f*6 + 1];
			dest_tris[f*2 + 0].vertex_indices[2] = indices[f*6 + 2];

			dest_tris[f*2 + 0].uv_indices[0] = getUVIndex(uvs[indices[f*6 + 0]]);
			dest_tris[f*2 + 0].uv_indices[1] = getUVIndex(uvs[indices[f*6 + 1]]);
			dest_tris[f*2 + 0].uv_indices[2] = getUVIndex(uvs[indices[f*6 + 2]]);
			dest_tris[f*2 + 0].setMatIndexAndUseShadingNormals(mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);

			dest_tris[f*2 + 1].vertex_indices[0] = indices[f*6 + 3];
			dest_tris[f*2 + 1].vertex_indices[1] = indices[f*6 + 4];
			dest_tris[f*2 + 1].vertex_indices[2] = indices[f*6 + 5];
			dest_tris[f*2 + 1].uv_indices[0] = getUVIndex(uvs[indices[f*6 + 3]]);
			dest_tris[f*2 + 1].uv_indices[1] = getUVIndex(uvs[indices[f*6 + 4]]);
			dest_tris[f*2 + 1].uv_indices[2] = getUVIndex(uvs[indices[f*6 + 5]]);
			dest_tris[f*2 + 1].setMatIndexAndUseShadingNormals(mat_index, RayMesh_ShadingNormals::RayMesh_NoShadingNormals);
		}
	}

	// Copy verts positions and normals
	raymesh->getVertices().resizeNoCopy(num_faces*4);
	RayMeshVertex* const dest_verts = raymesh->getVertices().data();
	for(size_t i=0; i<num_faces*4; ++i)
	{
		dest_verts[i].pos = verts[i];
		dest_verts[i].normal = normals[i];
	}

	// Set UVs (Note: only needed for dumping RayMesh to disk)
	raymesh->setMaxNumTexcoordSets(1);
	raymesh->getUVs().resize(4);
	raymesh->getUVs()[0] = Vec2f(0, 0);
	raymesh->getUVs()[1] = Vec2f(0, 1);
	raymesh->getUVs()[2] = Vec2f(1, 1);
	raymesh->getUVs()[3] = Vec2f(1, 0);

	Geometry::BuildOptions options;
	StandardPrintOutput print_output;
	raymesh->build(options, print_output, /*verbose=*/false, task_manager);

	raymesh_out = raymesh;

	Reference<OpenGLMeshRenderData> meshdata = new OpenGLMeshRenderData();
	meshdata->aabb_os = aabb_os;

	// Do a pass over voxels to count number of faces for each mat
	const int num_mat_batches = max_mat_index + 1;
	std::vector<int> mat_face_counts(num_mat_batches);
	for(size_t v=0; v<num_voxels; ++v)
		mat_face_counts[voxel_group.voxels[v].mat_index] += voxel_info[v].num_faces;

	// Loop over material face batches, and compute number of indices, starting byte offset etc..
	meshdata->has_uvs = true;
	meshdata->has_shading_normals = true;
	meshdata->batches.resize(num_mat_batches);
	int batch_offset = 0; // in faces
	std::vector<int> batch_write_indices(num_mat_batches); // in faces
	for(size_t i=0; i<num_mat_batches; ++i)
	{
		meshdata->batches[i].material_index = (uint32)i;
		meshdata->batches[i].num_indices = mat_face_counts[i] * 6; // 6 indices per face.
		meshdata->batches[i].prim_start_offset = batch_offset * 6 * sizeof(uint32); // Offset in bytes from the start of the index buffer.

		batch_write_indices[i] = batch_offset;
		batch_offset += mat_face_counts[i];
	}

	const int NUM_COMPONENTS = 8; // num float components per vertex.
	meshdata->vert_data.resizeNoCopy(num_faces*4 * NUM_COMPONENTS * sizeof(float)); // num verts = num_faces*4
	float* combined_data = (float*)meshdata->vert_data.data();
	
	js::Vector<uint32, 16>& sorted_indices = meshdata->vert_index_buffer;
	sorted_indices.resizeNoCopy(num_faces * 6);

	for(int v=0; v<(int)num_voxels; ++v)
	{
		const int src_face_offset = voxel_info[v].face_offset; // index into indices and verts, normals, uvs, for reading
		const int src_voxel_num_faces = voxel_info[v].num_faces; // num faces to read/write for this voxel.

		const int mat_index = voxel_group.voxels[v].mat_index;
		const int initial_write_i = batch_write_indices[mat_index]; // write index for this material, in faces.
		batch_write_indices[mat_index] += src_voxel_num_faces;

		int write_i = initial_write_i * 4 * NUM_COMPONENTS; // write index in floats.
		for(int f=src_face_offset; f<src_face_offset + src_voxel_num_faces; ++f) // f = src face index
		{
			// For each vert for face, copy vert data to combined_data
			for(int z=0; z<4; ++z)
			{
				const int src_vert_index = f*4 + z;
				const Vec3f& vertpos = verts[src_vert_index];

				combined_data[write_i + 0] = vertpos.x;
				combined_data[write_i + 1] = vertpos.y;
				combined_data[write_i + 2] = vertpos.z;
				combined_data[write_i + 3] = normals[src_vert_index].x;
				combined_data[write_i + 4] = normals[src_vert_index].y;
				combined_data[write_i + 5] = normals[src_vert_index].z;
				combined_data[write_i + 6] = uvs[src_vert_index].x;
				combined_data[write_i + 7] = uvs[src_vert_index].y;

				write_i += NUM_COMPONENTS;
			}
		}

		// Since the vertex data for this voxel may have moved during the sort, we need to update the indices for the faces in this voxel.
		const int vert_index_translation = (initial_write_i - src_face_offset) * 4;

		// Copy indices for the faces of this voxel to sorted_indices
		int indices_write_i = initial_write_i * 6;
		for(int z=0; z<src_voxel_num_faces*6; ++z)
		{
			const int new_vert_index = indices[src_face_offset*6 + z] + vert_index_translation;
			assert(new_vert_index >= 0 && new_vert_index < num_faces*4);
			sorted_indices[indices_write_i++] = new_vert_index;
		}
	}

	if(do_opengl_stuff)
	{
		meshdata->vert_vbo = new VBO(meshdata->vert_data.data(), meshdata->vert_data.dataSizeBytes());
		meshdata->vert_indices_buf = new VBO(sorted_indices.data(), sorted_indices.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
	}
	meshdata->index_type = GL_UNSIGNED_INT;

	VertexSpec& spec = meshdata->vertex_spec;
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

	if(do_opengl_stuff)
		meshdata->vert_vao = new VAO(meshdata->vert_vbo, spec);

	return meshdata;
}


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <indigo/TestUtils.h>


void ModelLoading::test()
{
	Indigo::TaskManager task_manager;
	
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
}


#endif
