/*=====================================================================
ModelLoading.h
--------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../shared/WorldMaterial.h"
#include "../shared/WorldObject.h"
#include <opengl/OpenGLEngine.h>
#include <dll/include/IndigoMesh.h>
#include <graphics/BatchedMesh.h>
#include <utils/Vector.h>


struct GLObject;
class Matrix4f;
class ResourceManager;
class RayMesh;
class PhysicsShape;
class VoxelGroup;
class VertexBufferAllocator;
namespace Indigo { class TaskManager; }


/*=====================================================================
ModelLoading
------------

=====================================================================*/
class ModelLoading
{
public:
	static void setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat);
	static void setGLMaterialFromWorldMaterial(const WorldMaterial& mat, int lod_level, const URLString& lightmap_url, bool use_basis, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat);


	static void checkValidAndSanitiseMesh(Indigo::Mesh& mesh);

	static void applyScaleToMesh(Indigo::Mesh& mesh, float scale);

	inline static bool isSupportedModelExtension(string_view extension);

	inline static bool hasSupportedModelExtension(const string_view path);

	// Load a model file from disk.
	// Also load associated material information from the model file.
	// Make an OpenGL object from it, suitable for previewing, so situated at the origin. 
	// Also make a BatchedMesh from it (unless we are loading a voxel format such as .vox).
	// If loading a voxel format, set loaded_object_out.voxel_group.
	// Set materials as well.
	// May compute a user-friendly scale and rotation for the object as well (For example rotating y-up to z-up)
	//
	// Throws glare::Exception on invalid mesh.
	SSE_CLASS_ALIGN MakeGLObjectResults
	{
	public:
		GLARE_ALIGNED_16_NEW_DELETE

		Matrix4f ob_to_world;
		GLObjectRef gl_ob;
		BatchedMeshRef batched_mesh; // Not set if we loaded a .vox model.
		//js::Vector<Voxel, 16> voxels; // Set if we loaded a .vox model.
		VoxelGroup voxels;
		std::vector<WorldMaterialRef> materials;
		Vec3f scale;
		Vec3f axis;
		float angle;
	};
	static void makeGLObjectForModelFile(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator, glare::Allocator* allocator, const std::string& model_path, bool do_opengl_stuff, MakeGLObjectResults& results_out);


	// This is a cube object with two materials, for displaying images or videos.
	// Sets loaded_object_in_out scale and materials
	static GLObjectRef makeImageCube(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator,
		const std::string& image_path, int im_w, int im_h,
		BatchedMeshRef& mesh_out,
		std::vector<WorldMaterialRef>& world_materials_out,
		Vec3f& scale_out
	);


	static GLObjectRef makeGLObjectForMeshDataAndMaterials(OpenGLEngine& gl_engine, const Reference<OpenGLMeshRenderData> gl_meshdata, /*size_t num_materials_referenced, */int ob_lod_level, const std::vector<WorldMaterialRef>& materials, 
		const URLString& lightmap_url, bool use_basis, ResourceManager& resource_manager, const Matrix4f& ob_to_world_matrix);

	static void setMaterialTexPathsForLODLevel(GLObject& gl_ob, int ob_lod_level, const std::vector<WorldMaterialRef>& materials,
		const URLString& lightmap_url, bool use_basis, ResourceManager& resource_manager);


	// Build OpenGLMeshRenderData and Physics shape from a mesh on disk identified by lod_model_path.
	static Reference<OpenGLMeshRenderData> makeGLMeshDataAndBatchedMeshForModelPath(const std::string& lod_model_path, ArrayRef<uint8> model_data_buf, VertexBufferAllocator* vert_buf_allocator, bool skip_opengl_calls, bool build_physics_ob, bool build_dynamic_physics_ob, 
		const js::Vector<bool>& create_physics_tris_for_mat,
		glare::Allocator* mem_allocator,
		PhysicsShape& physics_shape_out);

	// Build OpenGLMeshRenderData from voxel data.  Also return a reference to a physics shape.
	static Reference<OpenGLMeshRenderData> makeModelForVoxelGroup(const VoxelGroup& voxel_group, int subsample_factor, const Matrix4f& ob_to_world, 
		VertexBufferAllocator* vert_buf_allocator, bool do_opengl_stuff, bool need_lightmap_uvs, const js::Vector<bool, 16>& mats_transparent, bool build_dynamic_physics_ob,
		glare::Allocator* mem_allocator,
		PhysicsShape& physics_shape_out);

	//static Reference<BatchedMesh> makeBatchedMeshForVoxelGroup(const VoxelGroup& voxel_group);
	//static Reference<Indigo::Mesh> makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group);

	static void test();
};


bool ModelLoading::isSupportedModelExtension(string_view extension)
{
	return
		StringUtils::equalCaseInsensitive(extension, "bmesh") ||
		StringUtils::equalCaseInsensitive(extension, "vox") ||
		StringUtils::equalCaseInsensitive(extension, "obj") ||
		StringUtils::equalCaseInsensitive(extension, "stl") ||
		StringUtils::equalCaseInsensitive(extension, "gltf") ||
		StringUtils::equalCaseInsensitive(extension, "glb") ||
		StringUtils::equalCaseInsensitive(extension, "vrm") ||
		StringUtils::equalCaseInsensitive(extension, "igmesh");
}


bool ModelLoading::hasSupportedModelExtension(const string_view path)
{
	const string_view extension = getExtensionStringView(path);

	return isSupportedModelExtension(extension);
}
