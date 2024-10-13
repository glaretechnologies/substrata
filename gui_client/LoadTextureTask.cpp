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


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, const Reference<ResourceManager>& resource_manager_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, const ResourceRef& resource_,
	const TextureParams& tex_params_, bool is_terrain_map_)
:	opengl_engine(opengl_engine_), resource_manager(resource_manager_), result_msg_queue(result_msg_queue_), path(path_), resource(resource_), tex_params(tex_params_), is_terrain_map(is_terrain_map_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	try
	{
		// conPrint("LoadTextureTask: processing texture '" + path + "'");

		const std::string& key = this->path;

		// Load texture from disk and decode it.
		Reference<Map2D> map;
		if(hasExtension(key, "gif"))
			map = GIFDecoder::decodeImageSequence(key, opengl_engine->mem_allocator.ptr());
		else
		{
			ImageDecoding::ImageDecodingOptions options;
			options.ETC_support = opengl_engine->texture_compression_ETC_support;

			map = ImageDecoding::decodeImage(".", key, opengl_engine->mem_allocator.ptr(), options);
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




		if(hasExtension(key, "gif") && texture_data->totalCPUMemUsage() > 100000000)
		{
			conPrint("Large gif texture data: " + toString(texture_data->totalCPUMemUsage()) + " B, " + key);
		}


		// Send a message to MainWindow with the loaded texture data.
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = path;
		msg->tex_key = key;
		msg->tex_params = tex_params;
		msg->texture_data = texture_data;
		if(is_terrain_map)
			msg->terrain_map = map;

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
