/*=====================================================================
LoadTextureTask.cpp
-------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "LoadTextureTask.h"


#include "ThreadMessages.h"
#include "../shared/ImageDecoding.h"
#include "../shared/ResourceManager.h"
#include <indigo/TextureServer.h>
#include <graphics/ImageMapSequence.h>
#include <graphics/GifDecoder.h>
#include <graphics/CompressedImage.h>
#include <graphics/imformatdecoder.h> // For ImFormatExcep
#include <graphics/TextureProcessing.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/TextureAllocator.h>
#include <opengl/OpenGLUploadThread.h>
#include <utils/LimitedAllocator.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/IncludeHalf.h>
#include <utils/MemMappedFile.h>
#include <utils/FastPoolAllocator.h>
#include <tracy/Tracy.hpp>


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, const Reference<ResourceManager>& resource_manager_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const OpenGLTextureKey& path_, const ResourceRef& resource_,
	const TextureParams& tex_params_, bool is_terrain_map_, const Reference<glare::Allocator>& worker_allocator_, Reference<glare::FastPoolAllocator>& texture_loaded_msg_allocator_, const Reference<OpenGLUploadThread>& upload_thread_)
:	opengl_engine(opengl_engine_), resource_manager(resource_manager_), result_msg_queue(result_msg_queue_), path(path_), resource(resource_), tex_params(tex_params_), is_terrain_map(is_terrain_map_), 
	worker_allocator(worker_allocator_), texture_loaded_msg_allocator(texture_loaded_msg_allocator_), upload_thread(upload_thread_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	ZoneScopedN("LoadTextureTask"); // Tracy profiler
	ZoneText(this->path.c_str(), this->path.size());

	for(int attempt = 0; attempt < 10; ++attempt)
	{
		try
		{
			// conPrint("LoadTextureTask: processing texture '" + path + "'");

			const OpenGLTextureKey& key = this->path;


			runtimeCheck(resource.nonNull() && resource_manager.nonNull());
			ArrayRef<uint8> texture_data_buffer;
#if EMSCRIPTEN
			// Use the in-memory buffer that we loaded in EmscriptenResourceDownloader
			runtimeCheck(loaded_buffer.nonNull());
			texture_data_buffer = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
#else
			MemMappedFile file(key);
			texture_data_buffer = ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize());
#endif

			// Load texture from disk and decode it.
			Reference<Map2D> map;
			if(hasExtension(key, "gif"))
				map = GIFDecoder::decodeImageSequenceFromBuffer(texture_data_buffer.data(), texture_data_buffer.size(), worker_allocator.ptr());
			else
			{
				ImageDecoding::ImageDecodingOptions options;
				options.ETC_support = opengl_engine->texture_compression_ETC_support;

				map = ImageDecoding::decodeImageFromBuffer(/*base dir path (not used)=*/".", std::string(key), texture_data_buffer, worker_allocator.ptr(), options);
			}

#if USE_TEXTURE_VIEWS // NOTE: USE_TEXTURE_VIEWS is defined in opengl/TextureAllocator.h
			// Resize for texture view
			if(map.isType<ImageMap<half, HalfComponentValueTraits> >())
			{
			}
			if(map.isType<ImageMapUInt8>() || map.isType<ImageMapFloat>() || map.isType<ImageMap<half, HalfComponentValueTraits> >())
			{
				const size_t W = map->getMapWidth();
				const size_t H = map->getMapHeight();
				if(W <= 256 && H <= 256)
				{
					const size_t max_dim = myMax(W, H);
					const size_t power_2 = Maths::roundToNextHighestPowerOf2(max_dim);
		
					if(W != power_2 || H != power_2)
					{
						map = map->resizeMidQuality((int)power_2, (int)power_2, /*task manager=*/NULL);
					}
				}
			}
#endif
		
#if EMSCRIPTEN
			// There are a bunch of images, mostly lightmaps at LOD levels 1 and 2, that are compressed, and have width and height != a multiple of 4.
			// We can't use these in WebGL.  The proper solution is to rebuild them with better dimensions.
			if(dynamic_cast<CompressedImage*>(map.ptr()))
				if(!OpenGLTexture::areTextureDimensionsValidForCompression(*map))
					throw glare::Exception("Invalid texture for WebGL, is compressed and width or height is not a multiple of 4");
#endif

			Reference<TextureData> texture_data;
			if(const CompressedImage* compressed_img = dynamic_cast<const CompressedImage*>(map.ptr()))
			{
				texture_data = compressed_img->texture_data; // If the image map just holds already compressed data, use it.
			}
			else
			{
				const bool do_compression = opengl_engine->DXTTextureCompressionSupportedAndEnabled() && tex_params.allow_compression && OpenGLTexture::areTextureDimensionsValidForCompression(*map);

				texture_data = TextureProcessing::buildTextureData(map.ptr(), worker_allocator.ptr(), opengl_engine->getMainTaskManager(), do_compression, /*build_mipmaps=*/tex_params.use_mipmaps, /*convert_float_to_half=*/true);
			}


			if(hasExtension(key, "gif") && texture_data->totalCPUMemUsage() > 100000000)
			{
				conPrint("Large gif texture data: " + toString(texture_data->totalCPUMemUsage()) + " B, " + std::string(key));
			}

			if(upload_thread)
			{
				UploadTextureMessage* upload_msg = upload_thread->allocUploadTextureMessage();
				upload_msg->tex_path = path;
				upload_msg->tex_params = tex_params;
				upload_msg->texture_data = texture_data;
				upload_msg->frame_i = 0;

				LoadTextureTaskUploadingUserInfo* user_info = new LoadTextureTaskUploadingUserInfo();
				if(is_terrain_map)
					user_info->terrain_map = map;
				user_info->tex_URL = resource->URL;
				upload_msg->user_info = user_info;

				texture_data = NULL;

				upload_thread->getMessageQueue().enqueue(upload_msg);
			}
			else
			{
				// Send a message to MainWindow with the loaded texture data.
				glare::FastPoolAllocator::AllocResult res = this->texture_loaded_msg_allocator->alloc();
				Reference<TextureLoadedThreadMessage> msg = new (res.ptr) TextureLoadedThreadMessage();
				msg->allocator = texture_loaded_msg_allocator.ptr();
				msg->allocation_index = res.index;

				msg->tex_path = path;
				msg->tex_URL = resource->URL;
				msg->tex_params = tex_params;
				if(is_terrain_map)
					msg->terrain_map = map;
				msg->texture_data = texture_data;

				texture_data = NULL;

				result_msg_queue->enqueue(msg);
			}
			
			return;
		}
		catch(glare::LimitedAllocatorAllocFailed& e)
		{
			const int wait_time_ms = 1 << attempt;
			conPrint("LoadTextureTask: Got LimitedAllocatorAllocFailed, trying again in " + toString(wait_time_ms) + " ms: " + e.what());
			// Loop and try again, wait with exponential back-off.
			PlatformUtils::Sleep(wait_time_ms);
		}
		catch(ImFormatExcep& e)
		{
			result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + std::string(path) + "': " + e.what()));
			return;
		}
		catch(glare::Exception& e)
		{
			result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + std::string(path) + "': " + e.what()));
			return;
		}
		catch(std::bad_alloc&)
		{
			result_msg_queue->enqueue(new LogMessage("Error while loading texture: failed to allocate mem (bad_alloc)"));
			return;
		}
	}

	// We tried N times but each time we got an LimitedAllocatorAllocFailed exception.
	result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + std::string(path) + "': failed after multiple LimitedAllocatorAllocFailed"));
}
