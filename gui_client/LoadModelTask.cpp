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

			if(voxel_ob.nonNull())
			{
				ZoneText("Voxel", 5);

				const Matrix4f ob_to_world_matrix = obToWorldMatrix(*voxel_ob);

				if(voxel_ob->getCompressedVoxels().size() == 0)
				{
					throw glare::Exception("zero voxels");
				}
				else
				{
					VoxelGroup voxel_group;
					voxel_group.voxels.setAllocator(worker_allocator);
					WorldObject::decompressVoxelGroup(voxel_ob->getCompressedVoxels().data(), voxel_ob->getCompressedVoxels().size(), worker_allocator.ptr(), /*decompressed group out=*/voxel_group);

					const int max_model_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
					const int use_model_lod_level = myMin(model_lod_level, max_model_lod_level);

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
						need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape);

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

				gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(lod_model_path,
					model_buffer,
					/*vert_buf_allocator=*/NULL, 
					true, // skip_opengl_calls - we need to do these on the main thread.
					build_physics_ob,
					build_dynamic_physics_ob,
					worker_allocator.ptr(),
					/*physics shape out=*/physics_shape);
			}


			ArrayRef<uint8> vert_data;
			ArrayRef<uint8> index_data;
			if(gl_meshdata->batched_mesh)
			{
				vert_data = ArrayRef<uint8>(gl_meshdata->batched_mesh->vertex_data.data(), gl_meshdata->batched_mesh->vertex_data.dataSizeBytes());
				index_data = ArrayRef<uint8>(gl_meshdata->batched_mesh->index_data.data(), gl_meshdata->batched_mesh->index_data.dataSizeBytes());
			}
			else
			{
				vert_data = ArrayRef<uint8>(gl_meshdata->vert_data.data(), gl_meshdata->vert_data.dataSizeBytes());
				if(!gl_meshdata->vert_index_buffer.empty())
					index_data = ArrayRef<uint8>((const uint8*)gl_meshdata->vert_index_buffer.data(), gl_meshdata->vert_index_buffer.dataSizeBytes());
				else if(!gl_meshdata->vert_index_buffer_uint16.empty())
					index_data = ArrayRef<uint8>((const uint8*)gl_meshdata->vert_index_buffer_uint16.data(), gl_meshdata->vert_index_buffer_uint16.dataSizeBytes());
				else if(!gl_meshdata->vert_index_buffer_uint8.empty())
					index_data = ArrayRef<uint8>((const uint8*)gl_meshdata->vert_index_buffer_uint8.data(), gl_meshdata->vert_index_buffer_uint8.dataSizeBytes());
			}
			
			const size_t index_data_src_offset_B = Maths::roundUpToMultipleOfPowerOf2<size_t>(vert_data.size(), 16); // Offset in VBO
			const size_t total_geom_size_B = index_data_src_offset_B + index_data.size();

			VBORef vbo;
#if !EMSCRIPTEN
			for(int i=0; i<100; ++i)
			{
				vbo = opengl_engine->vbo_pool.getMappedVBO(total_geom_size_B);
				if(vbo)
				{
					// Copy mesh data to mapped VBO

					// Copy vertex data first
					std::memcpy(vbo->getMappedPtr(), vert_data.data(), vert_data.size());

					// Copy index data
					std::memcpy((uint8*)vbo->getMappedPtr() + index_data_src_offset_B, index_data.data(), index_data.size());

					// Free geometry memory now it has been copied to the PBO.
					if(gl_meshdata->batched_mesh)
					{
						gl_meshdata->batched_mesh->vertex_data.clearAndFreeMem();
						gl_meshdata->batched_mesh->index_data.clearAndFreeMem();
					}
					else
					{
						gl_meshdata->vert_data.clearAndFreeMem();
						gl_meshdata->vert_index_buffer.clearAndFreeMem();
						gl_meshdata->vert_index_buffer_uint16.clearAndFreeMem();
						gl_meshdata->vert_index_buffer_uint8.clearAndFreeMem();
					}

					break;
				}
				PlatformUtils::Sleep(1);
			}

			if(!vbo)
				conPrint("LoadModelTask: Failed to get mapped VBO for " + uInt32ToStringCommaSeparated((uint32)total_geom_size_B) + " B");
#endif


			// Send a ModelLoadedThreadMessage back to main window.
			Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
			msg->gl_meshdata = gl_meshdata;
			msg->physics_shape = physics_shape;
			msg->lod_model_url = lod_model_url;
			msg->model_lod_level = model_lod_level;
			msg->voxel_ob_uid = voxel_ob.nonNull() ? voxel_ob->uid : UID::invalidUID();
			msg->subsample_factor = subsample_factor;
			msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
			msg->vbo = vbo;
			msg->index_data_src_offset_B = index_data_src_offset_B;
			msg->total_geom_size_B = total_geom_size_B;
			msg->vert_data_size_B = vert_data.size();
			msg->index_data_size_B = index_data.size();

			// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
			// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
			gl_meshdata = NULL;
			physics_shape.jolt_shape = NULL;

			result_msg_queue->enqueue(msg);
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
			const std::string model_URL = voxel_ob ? "[voxel_ob]" : this->lod_model_url;
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': " + e.what()));
			return;
		}
		catch(std::bad_alloc&)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = voxel_ob ? "[voxel_ob]" : this->lod_model_url;
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': failed to allocate mem (bad_alloc)"));
			return;
		}
	}

	// We tried N times but each time we got an LimitedAllocatorAllocFailed exception.
	const std::string model_URL = voxel_ob ? "[voxel_ob]" : this->lod_model_url;
	result_msg_queue->enqueue(new LogMessage("Failed to load model '" + model_URL + "': failed after multiple LimitedAllocatorAllocFailed"));
}
