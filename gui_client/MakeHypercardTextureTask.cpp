/*=====================================================================
MakeHypercardTextureTask.cpp
----------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MakeHypercardTextureTask.h"


#include "MainWindow.h"
#include "WinterShaderEvaluator.h"
#include <QtGui/QPainter>
#include "../qt/QtUtils.h"
#include <ConPrint.h>
#include <PlatformUtils.h>


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

		Reference<TextureData> texture_data = TextureLoading::buildUInt8MapTextureData(map.ptr(), opengl_engine, &opengl_engine->getTaskManager());

		// Insert built texture data into texture manager
		opengl_engine->texture_data_manager->insertBuiltTextureData("hypercard_" + hypercard_content, texture_data);


		Reference<HypercardTexMadeMessage> msg = new HypercardTexMadeMessage();
		msg->hypercard_content = hypercard_content;
		main_window->msg_queue.enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while loading script: " + e.what());
	}
}
