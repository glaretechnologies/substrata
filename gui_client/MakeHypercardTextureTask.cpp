/*=====================================================================
MakeHypercardTextureTask.cpp
----------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MakeHypercardTextureTask.h"


#include "LoadTextureTask.h"
#include "WinterShaderEvaluator.h"
#include <graphics/ImageMap.h>
#include <graphics/TextureProcessing.h>
#include <opengl/OpenGLEngine.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#if USE_QT
#include "../qt/QtUtils.h"
#include <QtGui/QPainter>
#endif


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

		ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);
#if USE_SDL
		map->zero();
		// TEMP HACK REFACTOR TODO
#else
		QImage image(W, H, QImage::Format_RGB888);
		image.fill(QColor(220, 220, 220));
		QPainter painter(&image);
		painter.setPen(QPen(QColor(30, 30, 30)));
		painter.setFont(QFont("helvetica", 30, QFont::Normal));
		const int padding = 20;
		painter.drawText(QRect(padding, padding, W - padding*2, H - padding*2), Qt::AlignLeft/* | Qt::AlignVCenter*/, QtUtils::toQString(hypercard_content));

		// Copy to map
		for(int y=0; y<H; ++y)
		{
			const QRgb* line = (const QRgb*)image.scanLine(y);
			std::memcpy(map->getPixel(0, y), line, 3*W);
		}
#endif

		const bool allow_compression = opengl_engine->textureCompressionSupportedAndEnabled();
		Reference<TextureData> texture_data = TextureProcessing::buildTextureData(map.ptr(), opengl_engine->mem_allocator.ptr(), &opengl_engine->getTaskManager(), allow_compression, /*build mipmaps=*/true);

		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = tex_key;
		msg->tex_key = tex_key;
		msg->texture_data = texture_data;

		result_msg_queue->enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error in MakeHypercardTextureTask: " + e.what());
	}
}
