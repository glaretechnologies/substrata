/*=====================================================================
LoadTextureTask.cpp
-------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "LoadTextureTask.h"


#include "ThreadMessages.h"
#include "../shared/ImageDecoding.h"
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


#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT					0x8E8F


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, TextureServer* texture_server_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, 
	bool use_sRGB_, bool allow_compression_, bool is_terrain_map_, bool is_minimap_tile_)
:	opengl_engine(opengl_engine_), texture_server(texture_server_), result_msg_queue(result_msg_queue_), path(path_), use_sRGB(use_sRGB_), allow_compression(allow_compression_), is_terrain_map(is_terrain_map_),
	is_minimap_tile(is_minimap_tile_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	try
	{
		// conPrint("LoadTextureTask: processing texture '" + path + "'");

		const std::string key = texture_server->keyForPath(path); // Get canonical path.  May throw TextureServerExcep

		if(texture_server->isTextureLoadedForRawName(key)) // If this texture is already loaded, return.
			return;

		// Load texture from disk and decode it.
		Reference<Map2D> map;
		if(hasExtension(key, "gif"))
			map = GIFDecoder::decodeImageSequence(key);
		else
			map = ImageDecoding::decodeImage(".", key);

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

		const bool do_compression = opengl_engine->textureCompressionSupportedAndEnabled() && this->allow_compression;
		Reference<TextureData> texture_data = TextureProcessing::buildTextureData(map.ptr(), opengl_engine->mem_allocator.ptr(), &opengl_engine->getTaskManager(), do_compression, /*build_mipmaps=*/true);

		if(hasExtension(key, "gif") && texture_data->compressedSizeBytes() > 100000000)
		{
			conPrint("Large gif texture data: " + toString(texture_data->compressedSizeBytes()) + " B, " + key);
		}


		// Send a message to MainWindow with the loaded texture data.
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = path;
		msg->tex_key = key;
		msg->use_sRGB = use_sRGB;
		msg->texture_data = texture_data;
		if(is_terrain_map)
			msg->terrain_map = map;
		msg->is_minimap_tile = is_minimap_tile;
		result_msg_queue->enqueue(msg);
	}
	catch(TextureServerExcep& e)
	{
		conPrint("Warning: failed to get canonical key for path '" + path + "': " + e.what());
	}
	catch(ImFormatExcep& )
	{
		//conPrint("Warning: failed to decode texture '" + path + "': " + e.what());
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + path + "': " + e.what()));
	}
}
