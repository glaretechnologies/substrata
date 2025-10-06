/*=====================================================================
MakeHypercardTextureTask.h
--------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLTexture.h>
#include <Task.h>
#include <ThreadMessage.h>
#include <ThreadSafeQueue.h>
#include <string>
class OpenGLEngine;
class TextRendererFontFaceSizeSet;
class OpenGLUploadThread;
namespace glare { class FastPoolAllocator; }


/*=====================================================================
MakeHypercardTextureTask
------------------------

=====================================================================*/
class MakeHypercardTextureTask : public glare::Task
{
public:
	MakeHypercardTextureTask();
	virtual ~MakeHypercardTextureTask();

	virtual void run(size_t thread_index);

	OpenGLTextureKey tex_key;
	Reference<OpenGLEngine> opengl_engine;
	Reference<glare::FastPoolAllocator> texture_loaded_msg_allocator;
	Reference<TextRendererFontFaceSizeSet> fonts;
	std::string hypercard_content;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
	Reference<glare::Allocator> worker_allocator;

	Reference<OpenGLUploadThread> upload_thread;
};
