/*=====================================================================
ModelLoading.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../shared/WorldMaterial.h"
#include "../opengl/OpenGLEngine.h"
#include "../dll/include/IndigoMesh.h"
struct GLObject;
class Matrix4f;
class ResourceManager;
class RayMesh;
class VoxelGroup;
namespace Indigo { class TaskManager; }


struct MeshData
{
	Indigo::MeshRef mesh;
	Reference<OpenGLMeshRenderData> gl_meshdata;

	Reference<RayMesh> raymesh;
};


/*=====================================================================
MeshManager
-----------
Caches meshes and OpenGL data loaded from disk and built.
=====================================================================*/
class MeshManager
{
public:
	std::map<std::string, MeshData> model_URL_to_mesh_map;
};


/*=====================================================================
ModelLoading
-------------

=====================================================================*/
class ModelLoading
{
public:
	static void setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat);
	static void setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat);


	static void checkValidAndSanitiseMesh(Indigo::Mesh& mesh);

	// We don't have a material file, just the model file:
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelFile(const std::string& path, 
		const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, std::vector<WorldMaterialRef>& loaded_materials_out); // throws Indigo::Exception on failure.


	// For when we have materials:
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelURLAndMaterials(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials,
		ResourceManager& resource_manager, MeshManager& mesh_manager, Indigo::TaskManager& task_manager,
		const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, Reference<RayMesh>& raymesh_out); // throws Indigo::Exception on failure.


	static Reference<OpenGLMeshRenderData> makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out);


	static void test();
};

