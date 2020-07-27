/*=====================================================================
ModelLoading.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../shared/WorldMaterial.h"
#include "../shared/WorldObject.h"
#include <opengl/OpenGLEngine.h>
#include <dll/include/IndigoMesh.h>
#include <graphics/BatchedMesh.h>

struct GLObject;
class Matrix4f;
class ResourceManager;
class RayMesh;
class VoxelGroup;
namespace Indigo { class TaskManager; }


struct MeshData
{
	BatchedMeshRef mesh;
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
	bool isMeshDataInserted(const std::string& model_url) const;

	mutable Mutex mutex;
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
	static void setGLMaterialFromWorldMaterial(const WorldMaterial& mat, const std::string& lightmap_url, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat);


	static void checkValidAndSanitiseMesh(Indigo::Mesh& mesh);
	static void checkValidAndSanitiseMesh(BatchedMesh& mesh);


	// Load a mesh from disk.
	// Make an OpenGL object from it, also make an IndigoMesh from it (unless we are loading a voxel format such as .vox).
	// If loading a voxel format, set loaded_object_out.voxel_group.
	// Set loaded_object_out.materials as well.
	// May set a scale on loaded_object_out.
	//
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelFile(Indigo::TaskManager& task_manager, const std::string& path,
		BatchedMeshRef& mesh_out,
		WorldObject& loaded_object_out);


	// For when we have materials:
	//
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelURLAndMaterials(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials, const std::string& lightmap_url,
		ResourceManager& resource_manager, MeshManager& mesh_manager, Indigo::TaskManager& task_manager,
		const Matrix4f& ob_to_world_matrix, bool skip_opengl_calls, Reference<RayMesh>& raymesh_out);


	static Reference<OpenGLMeshRenderData> makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out);


	static void test();
};

