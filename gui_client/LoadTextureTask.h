/*=====================================================================
LoadTextureTask.h
-----------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "../shared/Resource.h"
#include <opengl/OpenGLTexture.h>
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


class TextureLoadedThreadMessage : public ThreadMessage
{
public:
	std::string tex_path;
	std::string tex_key;
	TextureParams tex_params;
	Reference<TextureData> texture_data;

	PBORef pbo; // Non-null if we wrote the texture mipmap data into the mapped memory region of a PBO.

	Reference<Map2D> terrain_map; // Non-null iff we are loading a terrain map (e.g. is_terrain_map is true)
};


/*=====================================================================
LoadTextureTask
---------------

=====================================================================*/
class LoadTextureTask : public glare::Task
{
public:
	LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, const Reference<ResourceManager>& resource_manager, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, const ResourceRef& resource,
		const TextureParams& tex_params, bool is_terrain_map, const Reference<glare::Allocator>& worker_allocator);

	virtual void run(size_t thread_index);

	Reference<OpenGLEngine> opengl_engine;
	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
	std::string path;
	ResourceRef resource;
	TextureParams tex_params;
	bool is_terrain_map;
	Reference<LoadedBuffer> loaded_buffer; // For emscripten, load from memory buffer instead of from resource on disk.Reference<LoadedBuffer> loaded_buffer;
	Reference<glare::Allocator> worker_allocator;
};
