/*=====================================================================
SubstrataVideoSurface.h
-----------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../opengl/OpenGLEngine.h"
#include <QtMultimedia/QAbstractVideoSurface>
#include <QtMultimedia/QVideoSurfaceFormat>
#include <QtMultimedia/QMediaPlayer>

#if 1
class SubstrataVideoSurface : public QAbstractVideoSurface
{
public:
	SubstrataVideoSurface(QObject *parent)
	:	QAbstractVideoSurface(parent),
		load_into_opengl_tex(true)
	{
	}

	virtual bool present(const QVideoFrame& frame_) override
	{
		//conPrint("SubVideoSurface: present()");
		//printVar(frame_.width());
		//printVar(frame_.height());
		//printVar(frame_.bytesPerLine());
		//printVar(frame_.isMapped());
		//printVar(frame_.isReadable());

		QVideoFrame frame = frame_; // Get non-const QVideoFrame. Just copies a pointer.

		const bool res = frame.map(QAbstractVideoBuffer::ReadOnly);

		const uchar* bits = frame.bits();

		if(res && bits && frame.width() > 0 && frame.height() > 0)
		{
			if(load_into_opengl_tex)
			{
				if(opengl_tex.nonNull())
					opengl_tex->loadIntoExistingTexture(frame.width(), frame.height(), frame.bytesPerLine(),
						ArrayRef<uint8>(bits, frame.height() * frame.bytesPerLine()));
			}
			else
			{
				frame_copy.resize(frame.height() * frame.bytesPerLine());
				std::memcpy(frame_copy.data(), bits, frame.height() * frame.bytesPerLine());
			}

			/*if(frame.handleType() == QAbstractVideoBuffer::GLTextureHandle)
			{
			this->opengl_tex->texture_handle = frame.handle().toUInt();
			}*/
		}

		frame.unmap();

		return true;
	}

	virtual bool start(const QVideoSurfaceFormat& format) override
	{
		//conPrint("SubstrataVideoSurface: start()");
		//printVar(format.frameWidth());
		//printVar(format.frameHeight());
		//printVar(format.handleType());
		//printVar(format.pixelFormat());
		
		this->current_format = format;
		
		/*switch(format.pixelFormat())
		{
		case QVideoFrame::Format_ARGB32:
			//conPrint("Format_ARGB32");
			break;
		case QVideoFrame::Format_RGB32:
			//conPrint("Format_RGB32");
			break;
		}*/

		//OpenGLTextureRef opengl_tex = new OpenGLTexture();
		//opengl_tex->loadWithFormats(format.frameWidth(), format.frameHeight(), tex_data_arrayref,
		//	ui->glWidget->opengl_engine.ptr(), OpenGLTexture::Format_SRGB_Uint8, // Format_RGB_Linear_Uint8, 
		//	GL_RGB, // GL internal format (num channels)
		//	GL_BGRA, // GL format.  Video frames are BGRA.
		//	OpenGLTexture::Filtering_Bilinear, OpenGLTexture::Wrapping_Repeat);

		if(load_into_opengl_tex)
		{
			opengl_tex = new OpenGLTexture(
				format.frameWidth(), format.frameHeight(),
				NULL, // opengl engine
				ArrayRef<uint8>(NULL, 0), // tex data
				OpenGLTexture::Format_SRGB_Uint8,
				GL_RGB, // GL internal format (num channels)
				GL_BGRA, // GL pixel format.  Video frames are BGRA.
				OpenGLTexture::Filtering_Bilinear,
				OpenGLTexture::Wrapping_Repeat
			);
		}

		//this->opengl_tex = new OpenGLTexture();

		//image_format = QVideoFrame::imageFormatFromPixelFormat(format.pixelFormat());

		return QAbstractVideoSurface::start(format); // "Note: You must call the base class implementation of start() at the end of your implementation."
	}

	virtual void stop() override
	{
		//conPrint("SubVideoSurface: stop()");


		QAbstractVideoSurface::stop(); // "Note: You must call the base class implementation of stop() at the start of your implementation."
	}

	virtual QList<QVideoFrame::PixelFormat>	supportedPixelFormats(QAbstractVideoBuffer::HandleType handle_type) const override
	{
		// NOTE: opengl texture rendering not supported with Qt on windows currently, see D3DPresentEngine::supportsTextureRendering()
		// in D:\programming\qt\qt-everywhere-src-5.13.2\qtmultimedia\src\plugins\common\evr\evrd3dpresentengine.cpp

		/*QList<QVideoFrame::PixelFormat> formats;
		formats.push_back(QVideoFrame::Format_RGB24);
		formats.push_back(QVideoFrame::Format_ARGB32);
		formats.push_back(QVideoFrame::Format_RGB32);
		formats.push_back(QVideoFrame::Format_BGRA32);
		formats.push_back(QVideoFrame::Format_ABGR32);
		formats.push_back(QVideoFrame::Format_BGR32);
		formats.push_back(QVideoFrame::Format_BGR24);
		return formats;*/
		if (handle_type == QAbstractVideoBuffer::GLTextureHandle) {
			/*return QList<QVideoFrame::PixelFormat>()
			<< QVideoFrame::Format_RGB32
			<< QVideoFrame::Format_ARGB32
			<< QVideoFrame::Format_ARGB32_Premultiplied
			<< QVideoFrame::Format_RGB565
			<< QVideoFrame::Format_RGB555;*/
			QList<QVideoFrame::PixelFormat> formats;
			formats.push_back(QVideoFrame::Format_RGB24);
			formats.push_back(QVideoFrame::Format_ARGB32);
			formats.push_back(QVideoFrame::Format_RGB32);
			formats.push_back(QVideoFrame::Format_BGRA32);
			formats.push_back(QVideoFrame::Format_ABGR32);
			formats.push_back(QVideoFrame::Format_BGR32);
			formats.push_back(QVideoFrame::Format_BGR24);
			return formats;
		} 
		else if (handle_type == QAbstractVideoBuffer::NoHandle) {
			/*return QList<QVideoFrame::PixelFormat>()
			<< QVideoFrame::Format_RGB32
			<< QVideoFrame::Format_ARGB32
			<< QVideoFrame::Format_ARGB32_Premultiplied
			<< QVideoFrame::Format_RGB565
			<< QVideoFrame::Format_RGB555;*/
			QList<QVideoFrame::PixelFormat> formats;
			formats.push_back(QVideoFrame::Format_RGB24);
			formats.push_back(QVideoFrame::Format_ARGB32);
			formats.push_back(QVideoFrame::Format_RGB32);
			formats.push_back(QVideoFrame::Format_BGRA32);
			formats.push_back(QVideoFrame::Format_ABGR32);
			formats.push_back(QVideoFrame::Format_BGR32);
			formats.push_back(QVideoFrame::Format_BGR24);
			return formats;
		} else {
			return QList<QVideoFrame::PixelFormat>();
		}
	}

	virtual bool isFormatSupported(const QVideoSurfaceFormat &format) const override
	{
		const QImage::Format imageFormat = QVideoFrame::imageFormatFromPixelFormat(format.pixelFormat());
		const QSize size = format.frameSize();

		return imageFormat != QImage::Format_Invalid
			&& !size.isEmpty()
			&& format.handleType() == QAbstractVideoBuffer::NoHandle;
	}

	OpenGLTextureRef opengl_tex;
	QVideoSurfaceFormat current_format;

	bool load_into_opengl_tex; // If false, just map frame and copy into frame_copy
	std::vector<uint8> frame_copy;
};
#endif