/*=====================================================================
LoadTextureTask.cpp
-------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "LoadTextureTask.h"


#include "MainWindow.h"
#include <indigo/TextureServer.h>
#include <graphics/imformatdecoder.h>
#include <opengl/OpenGLEngine.h>
#include <ConPrint.h>
#include <PlatformUtils.h>


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, const std::string& path_)
:	opengl_engine(opengl_engine_), main_window(main_window_), path(path_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	try
	{
		//conPrint("LoadTextureTask: processing texture '" + path + "'");

		const std::string key = main_window->texture_server->keyForPath(path); // Get canonical path.  May throw TextureServerExcep

		if(main_window->texture_server->isTextureLoadedForRawName(key)) // If this texture is already loaded, return.
			return;

		const bool just_inserted = main_window->checkAddTextureToProcessedSet(path); // Mark texture as being processed so another LoadTextureTask doesn't try and process it also.
		if(!just_inserted)
			return;

		Reference<Map2D> map = ImFormatDecoder::decodeImage(".", key);

		main_window->texture_server->insertTextureForRawName(map, key);

		// Process 8-bit textures (do DXT compression, mip-map computation etc..) in this thread.
		if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
		{
			const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

			Reference<TextureData> texture_data = TextureLoading::buildUInt8MapTextureData(imagemap, opengl_engine/*, main_window->build_uint8_map_scratch_state*/, /*multithread=*/true);

			// Give data to OpenGL engine
			opengl_engine->texture_data_manager->insertBuiltTextureData(imagemap, texture_data);
		}

		// Send a message to MainWindow saying the texture has been loaded
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->path = path;
		main_window->msg_queue.enqueue(msg);
	}
	catch(TextureServerExcep& e)
	{
		conPrint("Warning: failed to get canonical key for path '" + path + "': " + e.what());
	}
	catch(ImFormatExcep& )
	{
		//conPrint("Warning: failed to decode texture '" + path + "': " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Warning: failed to load texture '" + path + "': " + e.what());
	}
}
