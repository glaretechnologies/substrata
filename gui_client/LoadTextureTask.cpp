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
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <IncludeHalf.h>
#include <MemMappedFile.h>
#include <tracy/Tracy.hpp>


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, const Reference<ResourceManager>& resource_manager_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, const ResourceRef& resource_,
	const TextureParams& tex_params_, bool is_terrain_map_)
:	opengl_engine(opengl_engine_), resource_manager(resource_manager_), result_msg_queue(result_msg_queue_), path(path_), resource(resource_), tex_params(tex_params_), is_terrain_map(is_terrain_map_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	ZoneScopedN("LoadTextureTask"); // Tracy profiler
	ZoneText(this->path.c_str(), this->path.size());

	try
	{
		// conPrint("LoadTextureTask: processing texture '" + path + "'");

		const std::string& key = this->path;


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
			map = GIFDecoder::decodeImageSequenceFromBuffer(texture_data_buffer.data(), texture_data_buffer.size(), opengl_engine->mem_allocator.ptr());
		else
		{
			ImageDecoding::ImageDecodingOptions options;
			options.ETC_support = opengl_engine->texture_compression_ETC_support;

			map = ImageDecoding::decodeImageFromBuffer(/*base dir path (not used)=*/".", key, texture_data_buffer, opengl_engine->mem_allocator.ptr(), options);
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

			texture_data = TextureProcessing::buildTextureData(map.ptr(), opengl_engine->mem_allocator.ptr(), opengl_engine->getMainTaskManager(), do_compression, /*build_mipmaps=*/tex_params.use_mipmaps);
		}


		PBORef pbo;
		if(!texture_data->isMultiFrame())
		{
			ArrayRef<uint8> source_data;
			if(!texture_data->mipmap_data.empty())
				source_data = ArrayRef<uint8>(texture_data->mipmap_data.data(), texture_data->mipmap_data.size());
			else
			{
				runtimeCheck(texture_data->converted_image.nonNull());
				if(dynamic_cast<const ImageMapUInt8*>(texture_data->converted_image.ptr()))
				{
					const ImageMapUInt8* uint8_map = static_cast<const ImageMapUInt8*>(texture_data->converted_image.ptr());
					source_data = ArrayRef<uint8>(uint8_map->getData(), uint8_map->getDataSizeB());
				}
				if(dynamic_cast<const ImageMap<half, HalfComponentValueTraits>*>(texture_data->converted_image.ptr()))
				{
					const ImageMap<half, HalfComponentValueTraits>* half_map = static_cast<const ImageMap<half, HalfComponentValueTraits>*>(texture_data->converted_image.ptr());
					source_data = ArrayRef<uint8>((const uint8*)half_map->getData(), half_map->getDataSizeB());
				}
			}

			assert(source_data.data());
			if(source_data.data())
			{
				const int max_num_attempts = (texture_data->mipmap_data.size() < 1024 * 1024) ? 1000 : 10;
				for(int i=0; i<max_num_attempts; ++i)
				{
					pbo = opengl_engine->pbo_pool.getMappedPBO(source_data.size());
					if(pbo)
					{
						//conPrint("LoadTextureTask: Memcpying to PBO mem: " + toString(source_data.size()) + " B");
						std::memcpy(pbo->getMappedPtr(), source_data.data(), source_data.size()); // TODO: remove memcpy and build texture data directly into PBO

						// Free image texture memory now it has been copied to the PBO.
						texture_data->mipmap_data.clearAndFreeMem();
						if(texture_data->converted_image)
							texture_data->converted_image = nullptr;

						break;
					}
					PlatformUtils::Sleep(1);
				}
			}
			
			if(!pbo)
				conPrint("LoadTextureTask: Failed to get mapped PBO for " + toString(texture_data->mipmap_data.size()) + " B");
		}


		if(hasExtension(key, "gif") && texture_data->totalCPUMemUsage() > 100000000)
		{
			conPrint("Large gif texture data: " + toString(texture_data->totalCPUMemUsage()) + " B, " + key);
		}


		// Send a message to MainWindow with the loaded texture data.
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = path;
		msg->tex_key = key;
		msg->tex_params = tex_params;
		msg->pbo = pbo;
		if(is_terrain_map)
			msg->terrain_map = map;
		msg->texture_data = texture_data;

		texture_data = NULL;

		result_msg_queue->enqueue(msg);
	}
	catch(ImFormatExcep& e)
	{
		result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + path + "': " + e.what()));
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + path + "': " + e.what()));
	}
	catch(std::bad_alloc&)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading texture: failed to allocate mem (bad_alloc)"));
	}
}
