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
#include <graphics/imformatdecoder.h> // For ImFormatExcep
#include <opengl/OpenGLEngine.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <IncludeHalf.h>


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, TextureServer* texture_server_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, bool use_sRGB_)
:	opengl_engine(opengl_engine_), texture_server(texture_server_), result_msg_queue(result_msg_queue_), path(path_), use_sRGB(use_sRGB_)
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

		// Process 8-bit textures (do DXT compression, mip-map computation etc..) in this thread.
		bool is_8_bit = true;
		if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
		{
			const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

			Reference<TextureData> texture_data = TextureLoading::buildUInt8MapTextureData(imagemap, opengl_engine, &opengl_engine->getTaskManager());

			// Give data to OpenGL engine
			opengl_engine->texture_data_manager->insertBuiltTextureData(key, texture_data);
		}
		else if(dynamic_cast<const ImageMapSequenceUInt8*>(map.ptr()))
		{
			const ImageMapSequenceUInt8* imagemapseq = map.downcastToPtr<ImageMapSequenceUInt8>();

			Reference<TextureData> texture_data = TextureLoading::buildUInt8MapSequenceTextureData(imagemapseq, opengl_engine, &opengl_engine->getTaskManager());

			// Give data to OpenGL engine
			opengl_engine->texture_data_manager->insertBuiltTextureData(key, texture_data);
		}
		else
		{
			is_8_bit = false;

			// Convert 32-bit floating point images to half-precision floating point (16-bit) images.
			if(map.isType<ImageMapFloat>())
			{
				const ImageMapFloat* const image_map_float = map.downcastToPtr<ImageMapFloat>();
				Reference<ImageMap<half, HalfComponentValueTraits> > half_image = new ImageMap<half, HalfComponentValueTraits>(map->getMapWidth(), map->getMapHeight(), map->numChannels());
				
				const float* const src = image_map_float->getData();
				      half*  const dst = half_image->getData();
				const size_t data_size = image_map_float->numPixels() * map->numChannels();
				for(size_t i=0; i<data_size; ++i)
					dst[i] = half(src[i]);

				map = half_image;
			}


			texture_server->insertTextureForRawName(map, key);
		}

		// Send a message to MainWindow saying the texture has been loaded
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = path;
		msg->tex_key = key;
		msg->use_sRGB = use_sRGB;
		msg->tex_is_8_bit = is_8_bit;
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
