/*=====================================================================
LoadModelTask.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "LoadModelTask.h"


#include "LoadTextureTask.h"
#include "ThreadMessages.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <utils/LimitedAllocator.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FileUtils.h>
#include <utils/UniqueRef.h>
#include <utils/MemMappedFile.h>
#include <tracy/Tracy.hpp>


LoadModelTask::LoadModelTask()
:	build_physics_ob(true),
	build_dynamic_physics_ob(false),
	model_lod_level(-1)
{}


LoadModelTask::~LoadModelTask()
{}


void LoadModelTask::run(size_t thread_index)
{
	ZoneScopedN("LoadModelTask"); // Tracy profiler
	
	for(int attempt = 0; attempt < 10; ++attempt)
	{
		try
		{
			Reference<OpenGLMeshRenderData> gl_meshdata;
			PhysicsShape physics_shape;
			int subsample_factor = 1; // computed when loading voxels

			if(compressed_voxels)
			{
				ZoneText("Voxel", 5);

				assert(compressed_voxels->size() > 0);

				VoxelGroup voxel_group;
				voxel_group.voxels.setAllocator(worker_allocator);
				WorldObject::decompressVoxelGroup(compressed_voxels->data(), compressed_voxels->size(), worker_allocator.ptr(), /*decompressed group out=*/voxel_group);

				const int max_model_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
				const int use_model_lod_level = myMin(model_lod_level, max_model_lod_level);

				if(use_model_lod_level == 1)
					subsample_factor = 2;
				else if(use_model_lod_level == 2)
					subsample_factor = 4;

				// conPrint("Loading vox model for ob with UID " + voxel_ob->uid.toString() + " for LOD level " + toString(use_model_lod_level) + ", using subsample_factor " + toString(subsample_factor) + ", " + toString(voxel_group.voxels.size()) + " voxels");

				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
					need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape);
			}
			else // Else not voxel ob, just loading a model:
			{
				ZoneText(lod_model_url.c_str(), lod_model_url.size());

				assert(!lod_model_url.empty());
				runtimeCheck(resource.nonNull() && resource_manager.nonNull());

				// conPrint("LoadModelTask: loading mesh with URL '" + lod_model_url + "'.");

				const std::string lod_model_path = resource_manager->getLocalAbsPathForResource(*this->resource);

				UniqueRef<MemMappedFile> file;
				ArrayRef<uint8> model_buffer;
#if EMSCRIPTEN
				if(resource->external_resource)
				{
					// conPrint("LoadModelTask: '" + lod_model_url + "' is an external_resource, using MemMappedFile...");
					file.set(new MemMappedFile(lod_model_path));
					model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
				}
				else
				{
					// Use the in-memory buffer that we loaded in EmscriptenResourceDownloader
					if(!loaded_buffer)
						conPrint("LoadModelTask: loaded_buffer is null for resource with URL '" + lod_model_url + "'");
					runtimeCheck(loaded_buffer.nonNull());
					model_buffer = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
				}
#else
				// We want to load and build the mesh at lod_model_url.
			
				file.set(new MemMappedFile(lod_model_path));
				model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
#endif

				js::Vector<bool> create_tris_for_mat;

				gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(lod_model_path,
					model_buffer,
					/*vert_buf_allocator=*/NULL, 
					true, // skip_opengl_calls - we need to do these on the main thread.
					build_physics_ob,
					build_dynamic_physics_ob,
					create_tris_for_mat,
					worker_allocator.ptr(),
					/*physics shape out=*/physics_shape);
			}


			ArrayRef<uint8> vert_data, index_data;
			gl_meshdata->getVertAndIndexArrayRefs(vert_data, index_data);
			
			const size_t index_data_src_offset_B = Maths::roundUpToMultipleOfPowerOf2<size_t>(vert_data.size(), 16); // Offset in VBO
			const size_t total_geom_size_B = index_data_src_offset_B + index_data.size();

			if(upload_thread)
			{
				UploadGeometryMessage* upload_msg = new UploadGeometryMessage();
				upload_msg->meshdata = gl_meshdata;
				upload_msg->index_data_src_offset_B = index_data_src_offset_B;
				upload_msg->total_geom_size_B = total_geom_size_B;
				upload_msg->vert_data_size_B = vert_data.size();
				upload_msg->index_data_size_B = index_data.size();

				LoadModelTaskUploadingUserInfo* user_info = new LoadModelTaskUploadingUserInfo();
				user_info->physics_shape = physics_shape;
				user_info->lod_model_url = lod_model_url;
				user_info->model_lod_level = model_lod_level;
				user_info->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				user_info->voxel_subsample_factor = subsample_factor;
				user_info->voxel_hash = voxel_hash;

				upload_msg->user_info = user_info;

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				upload_thread->getMessageQueue().enqueue(upload_msg);
			}
			else
			{
				// Send a ModelLoadedThreadMessage back to main window.
				Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
				msg->gl_meshdata = gl_meshdata;
				msg->physics_shape = physics_shape;
				msg->lod_model_url = lod_model_url;
				msg->model_lod_level = model_lod_level;
				msg->voxel_hash = voxel_hash;
				msg->subsample_factor = subsample_factor;
				msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				msg->index_data_src_offset_B = index_data_src_offset_B;
				msg->total_geom_size_B = total_geom_size_B;
				msg->vert_data_size_B = vert_data.size();
				msg->index_data_size_B = index_data.size();

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				result_msg_queue->enqueue(msg);
			}

			return;
		}
		catch(glare::LimitedAllocatorAllocFailed& e)
		{
			const int wait_time_ms = 1 << attempt;
			conPrint("LoadModelTask: Got LimitedAllocatorAllocFailed, trying again in " + toString(wait_time_ms) + " ms: " + e.what());
			// Loop and try again, wait with exponential back-off.
			PlatformUtils::Sleep(wait_time_ms);
		}
		catch(glare::Exception& e)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': " + e.what()));
			return;
		}
		catch(std::bad_alloc&)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': failed to allocate mem (bad_alloc)"));
			return;
		}
	}

	// We tried N times but each time we got an LimitedAllocatorAllocFailed exception.
	const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
	result_msg_queue->enqueue(new LogMessage("Failed to load model '" + model_URL + "': failed after multiple LimitedAllocatorAllocFailed"));
}
