/*=====================================================================
LoadTextureTask.h
-----------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "../shared/Resource.h"
#include "../shared/UID.h"
#include <opengl/OpenGLTexture.h>
#include <opengl/OpenGLUploadThread.h>
#include <opengl/PBO.h>
#include <Task.h>
#include <ThreadMessage.h>
#include <ThreadSafeQueue.h>
#include <string>
class OpenGLEngine;
class TextureServer;
class TextureData;
class Map2D;
class ResourceManager;
class OpenGLUploadThread;
namespace glare { class FastPoolAllocator; }


class TextureLoadedThreadMessage : public ThreadMessage
{
public:
	TextureLoadedThreadMessage() : load_into_frame_i(0) {}

	OpenGLTextureKey tex_path;
	URLString tex_URL;
	TextureParams tex_params;
	Reference<TextureData> texture_data;
	Reference<OpenGLTexture> existing_opengl_tex; // When uploading a frame of an animated texture, upload into this already existing texture.
	int load_into_frame_i;

	Reference<Map2D> terrain_map; // Non-null iff we are loading a terrain map (e.g. is_terrain_map is true)
};

// Template specialisation of destroyAndFreeOb for TextureLoadedThreadMessage.  This is called when being freed by a Reference.
template <> inline void destroyAndFreeOb<TextureLoadedThreadMessage>(TextureLoadedThreadMessage* ob) { destroyAndFreeOb<ThreadMessage>(ob); }



struct LoadTextureTaskUploadingUserInfo : public UploadingUserInfo
{
	URLString tex_URL;
	Reference<Map2D> terrain_map; // Non-null iff we are loading a terrain map (e.g. is_terrain_map is true)
};


/*=====================================================================
LoadTextureTask
---------------

=====================================================================*/
class LoadTextureTask : public glare::Task
{
public:
	LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, const Reference<ResourceManager>& resource_manager, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const OpenGLTextureKey& path_, const ResourceRef& resource,
		const TextureParams& tex_params, bool is_terrain_map, const Reference<glare::Allocator>& worker_allocator, Reference<glare::FastPoolAllocator>& texture_loaded_msg_allocator,
		const Reference<OpenGLUploadThread>& upload_thread);

	virtual void run(size_t thread_index);

	Reference<OpenGLEngine> opengl_engine;
	Reference<ResourceManager> resource_manager;
	Reference<glare::FastPoolAllocator> texture_loaded_msg_allocator;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
	OpenGLTextureKey path;
	ResourceRef resource;
	TextureParams tex_params;
	bool is_terrain_map;
	Reference<LoadedBuffer> loaded_buffer; // For emscripten, load from memory buffer instead of from resource on disk.Reference<LoadedBuffer> loaded_buffer;
	Reference<glare::Allocator> worker_allocator;

	Reference<OpenGLUploadThread> upload_thread;
};
