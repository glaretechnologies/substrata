/*=====================================================================
LoadModelTask.h
---------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "../shared/Resource.h"
#include "../shared/URLString.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLUploadThread.h>
#include <utils/Task.h>
#include <utils/ThreadMessage.h>
#include <utils/ThreadSafeQueue.h>
#include <utils/SharedImmutableArray.h>
#include <string>
class OpenGLEngine;
class ResourceManager;


class ModelLoadedThreadMessage : public ThreadMessage
{
public:
	ModelLoadedThreadMessage() = default;
	GLARE_DISABLE_COPY(ModelLoadedThreadMessage);
	// Results of the task:
	
	Reference<OpenGLMeshRenderData> gl_meshdata;
	PhysicsShape physics_shape;
	
	URLString lod_model_url; // URL of the model we loaded.  Empty when loaded voxel object.
	int model_lod_level; // LOD level of the model we loaded.
	bool built_dynamic_physics_ob;

	int subsample_factor; // Computed when loading voxels.
	uint64 voxel_hash;

	// vert data offset = 0
	size_t index_data_src_offset_B;
	size_t total_geom_size_B;
	size_t vert_data_size_B; // in source VBO
	size_t index_data_size_B; // in source VBO
};


struct LoadModelTaskUploadingUserInfo : public UploadingUserInfo
{
	PhysicsShape physics_shape;
	URLString lod_model_url; // URL of the model we loaded.  Empty when loaded voxel object.
	int model_lod_level; // LOD level of the model we loaded.
	bool built_dynamic_physics_ob;

	int voxel_subsample_factor; // Computed when loading voxels.
	uint64 voxel_hash;
};


/*=====================================================================
LoadModelTask
-------------
Builds the OpenGL mesh and Physics mesh for a particular mesh or voxel group.

Once it's done, sends a ModelLoadedThreadMessage back to the main window
via result_msg_queue.

Note for making the OpenGL Mesh, data isn't actually loaded into OpenGL in this task,
since that needs to be done on the main thread.
=====================================================================*/
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif
class LoadModelTask : public glare::Task
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	LoadModelTask();
	virtual ~LoadModelTask();

	virtual void run(size_t thread_index);

	URLString lod_model_url; // The URL of a model with a specific LOD level to load.  Empty when loading voxel object.
	int model_lod_level; // The model LOD level of the object
	ResourceRef resource;
	bool build_physics_ob;
	bool build_dynamic_physics_ob; // If true, build a convex hull shape instead of a mesh physics shape.
	
	Reference<glare::SharedImmutableArray<uint8> > compressed_voxels;
	uint64 voxel_hash;
	js::Vector<bool> mat_transparent;
	bool need_lightmap_uvs;
	Matrix4f ob_to_world_matrix; // Used for generating lightmap coords for voxel meshes.

	Reference<LoadedBuffer> loaded_buffer; // For emscripten, load from memory buffer instead of from resource on disk.

	Reference<OpenGLEngine> opengl_engine;
	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;

	Reference<glare::Allocator> worker_allocator;

	Reference<OpenGLUploadThread> upload_thread;
};
#ifdef _WIN32
#pragma warning(pop)
#endif
