/*=====================================================================
MakeHypercardTextureTask.cpp
----------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MakeHypercardTextureTask.h"


#include "LoadTextureTask.h"
#include <graphics/ImageMap.h>
#include <graphics/TextureProcessing.h>
#include <graphics/TextRenderer.h>
#include <opengl/OpenGLEngine.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FastPoolAllocator.h>


MakeHypercardTextureTask::MakeHypercardTextureTask()
{}


MakeHypercardTextureTask::~MakeHypercardTextureTask()
{}


void MakeHypercardTextureTask::run(size_t thread_index)
{
	// conPrint("MakeHypercardTextureTask: hypercard_content: " + hypercard_content);

	try
	{
		// Make hypercard texture
		const int W = 512;
		const int H = 512;

		ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 1, worker_allocator.ptr());
		map->set(220);

		const int font_size_px = 30;
		Reference<TextRendererFontFace> font = fonts->getFontFaceForSize(/*font size px=*/font_size_px).ptr();
		
		const int padding = 20;
		font->drawText(*map, hypercard_content, padding, padding + font_size_px, Colour3f(30.f / 255.f), /*render SDF=*/false);


		// Don't do compression, as it will avoid compression's block artifacts, plus it allows us to use a 1-channel texture, which saves memory on platforms (mobile) where DXT compression isn't supported anyway.
		const bool allow_compression = false;
		Reference<TextureData> texture_data = TextureProcessing::buildTextureData(map.ptr(), worker_allocator.ptr(), opengl_engine->getMainTaskManager(), allow_compression, /*build mipmaps=*/true, /*convert_float_to_half=*/true);

		glare::FastPoolAllocator::AllocResult res = this->texture_loaded_msg_allocator->alloc();
		Reference<TextureLoadedThreadMessage> msg = new (res.ptr) TextureLoadedThreadMessage();
		msg->allocator = texture_loaded_msg_allocator.ptr();
		msg->allocation_index = res.index;

		msg->tex_path = tex_key;
		msg->texture_data = texture_data;

		result_msg_queue->enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error in MakeHypercardTextureTask: " + e.what());
	}
}
