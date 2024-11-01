/*=====================================================================
LoadModelTask.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoadModelTask.h"


#include "LoadTextureTask.h"
#include "ThreadMessages.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include <indigo/TextureServer.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <FileUtils.h>
#include <tracy/Tracy.hpp>


LoadModelTask::LoadModelTask()
:	build_physics_ob(true),
	build_dynamic_physics_ob(false)
{}


LoadModelTask::~LoadModelTask()
{}


void LoadModelTask::run(size_t thread_index)
{
	ZoneScopedN("LoadModelTask"); // Tracy profiler
	

	try
	{
		Reference<OpenGLMeshRenderData> gl_meshdata;
		PhysicsShape physics_shape;
		int subsample_factor = 1; // computed when loading voxels

		if(voxel_ob.nonNull())
		{
			ZoneText("Voxel", 5);

			const Matrix4f ob_to_world_matrix = obToWorldMatrix(*voxel_ob);

			if(voxel_ob->getCompressedVoxels().size() == 0)
			{
				// Add dummy cube marker for zero-voxel case.
				gl_meshdata = opengl_engine->getCubeMeshData();
				physics_shape = unit_cube_shape;
			}
			else
			{
				VoxelGroup voxel_group;
				voxel_group.voxels.setAllocator(opengl_engine->mem_allocator);
				WorldObject::decompressVoxelGroup(voxel_ob->getCompressedVoxels().data(), voxel_ob->getCompressedVoxels().size(), opengl_engine->mem_allocator.ptr(), /*decompressed group out=*/voxel_group);

				const int max_model_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
				const int use_model_lod_level = myMin(voxel_ob_model_lod_level/*model_lod_level*/, max_model_lod_level);

				// conPrint("!!!!!!!!!!!!!! LoadModelTask: Loading voxel ob " + toHexString((uint64)voxel_ob.ptr()) + " with use_model_lod_level " + toString(use_model_lod_level));

				if(use_model_lod_level == 1)
					subsample_factor = 2;
				else if(use_model_lod_level == 2)
					subsample_factor = 4;

				js::Vector<bool, 16> mat_transparent(voxel_ob->materials.size());
				for(size_t i=0; i<voxel_ob->materials.size(); ++i)
					mat_transparent[i] = voxel_ob->materials[i]->opacity.val < 1.f;

				// conPrint("Loading vox model for ob with UID " + voxel_ob->uid.toString() + " for LOD level " + toString(use_model_lod_level) + ", using subsample_factor " + toString(subsample_factor) + ", " + toString(voxel_group.voxels.size()) + " voxels");

				const bool need_lightmap_uvs = !voxel_ob->lightmap_url.empty();
				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
					need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, opengl_engine->mem_allocator.ptr(), /*physics shape out=*/physics_shape);

				// Temp for testing: Save voxels to disk.
				/*if(voxel_ob->uid.value() == 171111)
				{
					conPrint("Writing voxdata to disk for debugging...");
					FileUtils::writeEntireFile("d:/files/voxeldata/ob_" + voxel_ob->uid.toString() + "_voxeldata.voxdata", (const char*)voxel_group.voxels.data(), voxel_group.voxels.dataSizeBytes());
					FileUtils::writeEntireFile("d:/files/voxeldata/ob_" + voxel_ob->uid.toString() + "_voxeldata_compressed.compressedvoxdata", (const char*)voxel_ob->getCompressedVoxels().data(), voxel_ob->getCompressedVoxels().size());
				}*/
			}
		}
		else // Else not voxel ob, just loading a model:
		{
			ZoneText(lod_model_url.c_str(), lod_model_url.size());

			assert(!lod_model_url.empty());

			// We want to load and build the mesh at lod_model_url.
			// conPrint("LoadModelTask: loading mesh with URL '" + lod_model_url + "'.");
			const std::string lod_model_path = resource_manager->pathForURL(lod_model_url);

			gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(lod_model_path,
				/*vert_buf_allocator=*/NULL, 
				true, // skip_opengl_calls - we need to do these on the main thread.
				build_physics_ob,
				build_dynamic_physics_ob,
				opengl_engine->mem_allocator.ptr(),
				/*physics shape out=*/physics_shape);
		}

		// Send a ModelLoadedThreadMessage back to main window.
		Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
		msg->gl_meshdata = gl_meshdata;
		msg->physics_shape = physics_shape;
		msg->lod_model_url = lod_model_url;
		msg->voxel_ob_uid = voxel_ob.nonNull() ? voxel_ob->uid : UID::invalidUID();
		msg->voxel_ob_model_lod_level = voxel_ob_model_lod_level;
		msg->subsample_factor = subsample_factor;
		msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;

		// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
		// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
		gl_meshdata = NULL;
		physics_shape.jolt_shape = NULL;

		result_msg_queue->enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading model: " + e.what()));
	}
	catch(std::bad_alloc&)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading model: failed to allocate mem (bad_alloc)"));
	}


#if EMSCRIPTEN
	if(resource.nonNull())
	{
		try
		{
			resource_manager->deleteResourceLocally(resource);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: excep while deleting resource locally: " + e.what());
		}
	}
#endif
}
