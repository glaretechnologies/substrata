/*=====================================================================
AnimatedTextureManager.cpp
--------------------------
Copyright Glare Technologies Limited 2021-
=====================================================================*/
#include "AnimatedTextureManager.h"


#include "MainWindow.h"
#include "../shared/WorldObject.h"
#include "../video/WMFVideoReader.h"
#include "../qt/QtUtils.h"
#include "Escaping.h"
#include "../direct3d/Direct3DUtils.h"
#include <QtMultimedia/QAbstractVideoSurface>
#include <QtMultimedia/QVideoSurfaceFormat>
#include <QtMultimedia/QMediaPlayer>
#include "FileInStream.h"


class ResourceVidReaderByteStream;


class SubVideoSurface : public QAbstractVideoSurface
{
public:
	SubVideoSurface(QObject *parent)
		:	QAbstractVideoSurface(parent)
	{
	}

	virtual bool present(const QVideoFrame& frame_) override
	{
		//conPrint("SubVideoSurface: present()");

		QVideoFrame frame = frame_; // Get non-const QVideoFrame. Just copies a pointer.
		const bool res = frame.map(QAbstractVideoBuffer::ReadOnly);

		const uchar* bits = frame.bits();

		//printVar(frame.width());
		//printVar(frame.height());
		//printVar(frame.bytesPerLine());
		//printVar(frame.isMapped());
		//printVar(frame.isReadable());

		if(res && bits && frame.width() > 0 && frame.height() > 0)
		{
			opengl_tex->load(frame.width(), frame.height(), frame.bytesPerLine(),
				ArrayRef<uint8>(bits, frame.height() * frame.bytesPerLine()));

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
		//conPrint("SubVideoSurface: start()");

		//printVar(format.frameWidth());
		//printVar(format.frameHeight());
		//printVar(format.handleType());
		//printVar(format.pixelFormat());

		switch(format.pixelFormat())
		{
		case QVideoFrame::Format_ARGB32:
			//conPrint("Format_ARGB32");
			break;
		case QVideoFrame::Format_RGB32:
			//conPrint("Format_RGB32");
			break;
		}

		//OpenGLTextureRef opengl_tex = new OpenGLTexture();
		//opengl_tex->loadWithFormats(format.frameWidth(), format.frameHeight(), tex_data_arrayref,
		//	ui->glWidget->opengl_engine.ptr(), OpenGLTexture::Format_SRGB_Uint8, // Format_RGB_Linear_Uint8, 
		//	GL_RGB, // GL internal format (num channels)
		//	GL_BGRA, // GL format.  Video frames are BGRA.
		//	OpenGLTexture::Filtering_Bilinear, OpenGLTexture::Wrapping_Repeat);

		opengl_tex = new OpenGLTexture(
			format.frameWidth(), format.frameHeight(),
			NULL, // opengl engine
			OpenGLTexture::Format_SRGB_Uint8,
			GL_RGB, // GL internal format (num channels)
			GL_BGRA, // GL pixel format.  Video frames are BGRA.
			OpenGLTexture::Filtering_Bilinear,
			OpenGLTexture::Wrapping_Repeat
		);

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
		// NOTE: opengl texture rendering not supported on windows currently, see D3DPresentEngine::supportsTextureRendering()
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
};


AnimatedTexData::AnimatedTexData()
:	media_player(NULL), video_surface(NULL), cur_frame_i(0), in_anim_time(0), encounted_error(false), at_vidreader_EOS(false), num_samples_pending(0)
#ifdef _WIN32
	, locked_interop_tex_ob(0)
#endif
{}

AnimatedTexData::~AnimatedTexData()
{}


void AnimatedTexData::shutdown(
#ifdef _WIN32
	WGL& wgl_funcs, HANDLE interop_device_handle
#endif
)
{
	delete media_player;
	media_player = NULL;

	delete video_surface;
	video_surface = NULL;

	resource_io_wrapper = NULL;


	video_reader = NULL; // Make sure to destroy video reader before frameinfos queue as it has a pointer to frameinfos.

#ifdef _WIN32
	// Unlock the currently locked interop handle, if we have a locked one.
	if(locked_interop_tex_ob != NULL)
	{
		const BOOL res = wgl_funcs.wglDXUnlockObjectsNV(interop_device_handle, /*count=*/1, &locked_interop_tex_ob);
		if(!res)
			conPrint("Warning: wglDXUnlockObjectsNV failed.");
		locked_interop_tex_ob = NULL;
	}

	// Free interop handles
	for(auto entry : opengl_tex_for_d3d_tex)
	{
		const BOOL res = wgl_funcs.wglDXUnregisterObjectNV(interop_device_handle, entry.second.interop_handle);
		if(!res)
			conPrint("Warning: wglDXUnregisterObjectNV failed.");
	}

	opengl_tex_for_d3d_tex.clear();
#endif
}


class SubstrataVideoReaderCallback : public VideoReaderCallback
{
public:
	virtual void frameDecoded(VideoReader* vid_reader, const SampleInfoRef& frameinfo)
	{
		//conPrint("frameDecoded, time " + toString(frameinfo.frame_time));

		sample_queue->enqueue(frameinfo);
	}


	virtual void endOfStream(VideoReader* vid_reader)
	{
		//conPrint("endOfStream()");

		// insert an empty FrameInfo to signify EOS
		sample_queue->enqueue(NULL);
	}

	ThreadSafeQueue<SampleInfoRef>* sample_queue;
};


// For constructing WMFVideoReader not in a main thread, since the constructor takes a while.
struct CreateVidReaderTask : public glare::Task
{
	virtual void run(size_t thread_index)
	{
		try
		{
#if defined(_WIN32)
			Reference<WMFVideoReader> vid_reader_ = new WMFVideoReader(/*read from vid device=*/false, URL, /*vid_reader_byte_stream, */callback, dev_manager, /*decode_to_d3d_tex=*/true);

			Lock lock(mutex);
			this->vid_reader = vid_reader_;
#endif
		}
		catch(glare::Exception& e)
		{
			Lock lock(mutex);
			error_msg = e.what();
		}
	}

	//Reference<VidReaderByteStream> vid_reader_byte_stream;
	std::string URL;
	SubstrataVideoReaderCallback* callback;
	IMFDXGIDeviceManager* dev_manager;

	Mutex mutex; // protects vid_reader and error_msg
	Reference<VideoReader> vid_reader;
	std::string error_msg; // Set to a non-empty string on failure.
};


// A wrapper around a resource in-mem buffer that implements the QIODevice interface, which is used by QMediaPlayer.
// See D:\programming\qt\qt-everywhere-src-5.15.2\qtbase\src\corelib\io\qbuffer.cpp for similar example implementation.
class ResourceIODeviceWrapper : public QIODevice, public ThreadSafeRefCounted
{
public:
	ResourceIODeviceWrapper(ResourceRef resource_) : resource(resource_)
	{
		open(QIODevice::ReadOnly);

		Lock lock(resource->buffer_mutex);
		resource->num_buffer_readers++; // Increase reader count, so buffer is not cleared while we are reading from it.
	}

	virtual ~ResourceIODeviceWrapper()
	{
		Lock lock(resource->buffer_mutex);
		resource->num_buffer_readers--;
	}

	// "Subclasses of QIODevice are only required to implement the protected readData() and writeData() functions"
	// It seems we needd to implement size() as well however.

	virtual qint64 readData(char* data, qint64 maxSize) override
	{
		const int64 cur_pos = pos();

		Lock lock(resource->buffer_mutex);
		const int64 available = (int64)resource->buffer.size() - cur_pos;
		if(available > 0)
		{
			const int64 read_amount = myMin(maxSize, available);
			std::memcpy(data, &resource->buffer[cur_pos], read_amount);
			return read_amount;
		}
		else // else no bytes available currently:
		{
			if(resource->buffer.size() == resource->buffer.capacity()) // If the resource is already completely downloaded to buffer:
				return -1;
			else
				return 0; // else still downloading, might be readable data later.
		}
	}

	virtual qint64 writeData(const char *data, qint64 maxSize) override
	{
		return -1;
	}

	virtual qint64 size() const override
	{
		Lock lock(resource->buffer_mutex);
		return resource->buffer.capacity();
	}

	virtual bool isSequential() const override
	{
		return false;
	}

	ResourceRef resource;
};


#if 0
class ResourceVidReaderByteStream : public VidReaderByteStream, public ResourceDownloadListener
{
public:
	ResourceVidReaderByteStream(ResourceRef resource_) : resource(resource_), cur_i(0)
	{
		Lock lock(resource->buffer_mutex);
		resource->num_buffer_readers++; // Increase reader count, so buffer is not cleared while we are reading from it.

		resource->addDownloadListener(this);
	}

	virtual ~ResourceVidReaderByteStream()
	{
		resource->removeDownloadListener(this);

		Lock lock(resource->buffer_mutex);
		resource->num_buffer_readers--;
	}

	virtual bool isNetworkSource() const override { return true; }

	virtual uint64 currentPos() const override
	{
		return cur_i;
	}

	virtual uint64 length() const override
	{
		Lock lock(resource->buffer_mutex);
		return resource->buffer.capacity();
	}

	virtual uint64 readableLength() const override
	{
		Lock lock(resource->buffer_mutex);
		return resource->buffer.size();
	}

	virtual bool readable() const override
	{
		Lock lock(resource->buffer_mutex);
		return cur_i < resource->buffer.size();
	}

	virtual uint64 read(uint8* buffer, uint64 buffer_len) override // Returns amount of data read.
	{
		conPrint("ResourceVidReaderByteStream: reading up to " + toString(buffer_len) + " B at offset " + toString(cur_i) + "... (cur buf size: " + toString(resource->buffer.size()) + ")");
		Lock lock(resource->buffer_mutex);
		const int64 available = (int64)resource->buffer.size() - cur_i;
		if(available > 0)
		{
			const int64 read_amount = myMin((int64)buffer_len, available);
			std::memcpy(buffer, &resource->buffer[cur_i], read_amount);
			conPrint("	read " + toString(read_amount) + " B");
			cur_i += read_amount;
			return read_amount;
		}
		else // else no bytes available currently:
		{
			conPrint("	read 0 B");
			//if(resource->buffer.size() == resource->buffer.capacity()) // If the resource is already completely downloaded to buffer:
			//	return -1;
			//else
				return 0; // else still downloading, might be readable data later.
		}
	}

	virtual uint64 readAtPos(size_t pos, uint8* buffer, uint64 buffer_len) // Returns amount of data read. don't advance read position
	{
		conPrint("ResourceVidReaderByteStream: readAtPos(): reading up to " + toString(buffer_len) + " B at offset " + toString(pos) + "... (cur buf size: " + toString(resource->buffer.size()) + ")");
		Lock lock(resource->buffer_mutex);
		const int64 available = (int64)resource->buffer.size() - pos;
		if(available > 0)
		{
			const int64 read_amount = myMin((int64)buffer_len, available);
			conPrint("Copying " + toString(read_amount) + " B to buffer " + toHexString((uint64)buffer));
			std::memcpy(buffer, &resource->buffer[pos], read_amount);
			conPrint("	read " + toString(read_amount) + " B");
			return read_amount;
		}
		else // else no bytes available currently:
		{
			conPrint("	read 0 B");
			//if(resource->buffer.size() == resource->buffer.capacity()) // If the resource is already completely downloaded to buffer:
			//	return -1;
			//else
			return 0; // else still downloading, might be readable data later.
		}
	}


	virtual void setCurrentPos(uint64 p) override
	{
		conPrint("Seeking to " + toString(p));
		cur_i = p;
	}


	virtual void addListener(VidReaderByteStreamListener* listener) override
	{
		listeners.insert(listener);
	}
	virtual void removeListener(VidReaderByteStreamListener* listener) override
	{
		listeners.erase(listener);
	}

	//-------------------------- ResourceDownloadListener interface --------------------------
	virtual void dataReceived()
	{
		for(auto it : listeners)
			it->dataReceived();
		//this->doDataReceived();
	}


	std::set<VidReaderByteStreamListener*> listeners;
	ResourceRef resource;
	uint64 cur_i;
};
#endif

#if 0
class FileInStreamByteStream : public VidReaderByteStream
{
public:
	FileInStreamByteStream(const std::string& path) : file(path) {}

	virtual bool isNetworkSource() const override { return false; }

	virtual uint64 currentPos() const override
	{
		return file.getReadIndex();
	}

	virtual uint64 length() const override
	{
		return file.fileSize();
	}

	virtual uint64 read(uint8* buffer, uint64 buffer_len) override // Returns amount of data read.
	{
		const int64 read_size = myMin((int64)buffer_len, (int64)file.fileSize() - (int64)file.getReadIndex());

		if(read_size > 0)
		{
			file.readData(buffer, read_size);
			return read_size;
		}
		else
			return 0;
	}

	virtual void setCurrentPos(uint64 p) override
	{
		file.setReadIndex(p);
	}

	FileInStream file;
};
#endif


void AnimatedTexObData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	if(ob->opengl_engine_ob.nonNull())
	{
		AnimatedTexObData& animation_data = *this;
		animation_data.mat_animtexdata.resize(ob->opengl_engine_ob->materials.size());

		for(size_t m=0; m<ob->opengl_engine_ob->materials.size(); ++m)
		{
			OpenGLMaterial& mat = ob->opengl_engine_ob->materials[m];
			if(animation_data.mat_animtexdata[m].isNull())
				animation_data.mat_animtexdata[m] = new AnimatedTexData(); // TODO: not needed for all mats
			AnimatedTexData& animtexdata = *animation_data.mat_animtexdata[m];

			if(mat.albedo_texture.nonNull() && hasExtensionStringView(mat.tex_path, "gif"))
			{
				// Fetch the texdata for this texture if we haven't already

				if(animtexdata.texdata.isNull())
					animtexdata.texdata = opengl_engine->texture_data_manager->getTextureData(mat.tex_path);

				TextureData* texdata = animtexdata.texdata.ptr();

				if(texdata && !texdata->frames.empty()) // Check !frames.empty() for back() calls below.
				{
					const double total_anim_time = texdata->frame_start_times.back() + texdata->frame_durations.back();

					const double in_anim_time = Maths::doubleMod(anim_time, total_anim_time);

					/*
					frame 0                     frame 1                        frame 2                      frame 3
					|----------------------------|-----------------------------|-----------------------------|--------------------------------|----------> time
					^                                         ^
					cur_frame_i                             in anim_time
					*/

					// Advance current frame as needed, until frame_start_times[cur_frame_i + 1] >= in_anim_time, or cur_frame_i is the last frame
					while(((animtexdata.cur_frame_i + 1) < (int)texdata->frame_start_times.size()) && (texdata->frame_start_times[animtexdata.cur_frame_i + 1] < in_anim_time))
						animtexdata.cur_frame_i++;

					if(in_anim_time <= texdata->frame_durations[0]) // If the in-anim time has looped back so that it's in frame 0:
						animtexdata.cur_frame_i = 0;

					if(animtexdata.cur_frame_i >= 0 && animtexdata.cur_frame_i < (int)texdata->frames.size()) // Make sure in bounds
						TextureLoading::loadIntoExistingOpenGLTexture(mat.albedo_texture, *texdata, animtexdata.cur_frame_i, opengl_engine);
				}
			}
			else if(hasExtensionStringView(mat.tex_path, "mp4"))
			{
				const double ob_dist_from_cam = ob->pos.getDist(main_window->cam_controller.getPosition());
				const double max_play_dist = 20.0;
				const bool in_process_dist = ob_dist_from_cam < max_play_dist; // Only play videos within X metres for now.
				
#if defined(_WIN32)
				const bool USE_QT_TO_PLAY_MP4S = false; // Use WMFVideoReader instead.
#else
				const bool USE_QT_TO_PLAY_MP4S = true;
#endif

				if(animtexdata.media_player != NULL)
				{
					// Pause when out of range and playing, resume when in range and paused.
					if(!in_process_dist && (animtexdata.media_player->state() == QMediaPlayer::PlayingState))
					{
						//conPrint("Pausing");
						animtexdata.media_player->pause();
					}
					else if (in_process_dist && (animtexdata.media_player->state() == QMediaPlayer::PausedState))
					{
						animtexdata.media_player->play();
						//conPrint("Playing");
					}
				}

				if(in_process_dist)
				{
					if(in_process_dist && ob->audio_source.nonNull())
					{
						main_window->audio_engine.setSourcePosition(ob->audio_source, ob->opengl_engine_ob->aabb_ws.centroid());
					}

					try
					{
						// Qt MediaPlayer playback:
						if(USE_QT_TO_PLAY_MP4S)
						{
							if(animtexdata.media_player == NULL)
							{
								ResourceRef resource = main_window->resource_manager->getExistingResourceForURL(mat.tex_path);
								if(resource.nonNull())
								{
									if(resource->getState() == Resource::State_Transferring)
									{
										animtexdata.resource_io_wrapper = new ResourceIODeviceWrapper(resource);

										animtexdata.video_surface = new SubVideoSurface(NULL);
							
										animtexdata.media_player = new QMediaPlayer(NULL, QMediaPlayer::VideoSurface);
										animtexdata.media_player->setVideoOutput(animtexdata.video_surface);
										animtexdata.media_player->setMedia(QUrl(QtUtils::toQString(mat.tex_path)), animtexdata.resource_io_wrapper.getPointer());
										animtexdata.media_player->play();
									}
									else if(resource->getState() == Resource::State_Present)
									{
										// Read off disk
										const std::string disk_path = resource->getLocalPath();

										animtexdata.video_surface = new SubVideoSurface(NULL);

										animtexdata.media_player = new QMediaPlayer(NULL, QMediaPlayer::VideoSurface);
										animtexdata.media_player->setVideoOutput(animtexdata.video_surface);
										animtexdata.media_player->setMedia(QUrl::fromLocalFile(QtUtils::toQString(disk_path)));
										animtexdata.media_player->play();
									}
								}
							}
							else // else if animtexdata.media_player != NULL
							{
								//printVar(animtexdata.media_player->isSeekable());
								if(animtexdata.media_player->mediaStatus() == QMediaPlayer::EndOfMedia)
								{
									animtexdata.media_player->setPosition(0); // Seek to beginning
									animtexdata.media_player->play();
									//conPrint("Seeked to beginning");
								}

								// Vaguely try and match the volume of the spatial audio and WMFVideoReader code.
								const Vec3d campos = main_window->cam_controller.getPosition();
								const double dist = ob->pos.getDist(campos);

								// Subtract 100/max_play_dist so at dist = max_play_dist the volume is zero.
								const double vol = myMin(100.0, 100 / dist - 100.0 / max_play_dist);
								animtexdata.media_player->setVolume((int)vol);

								if(animtexdata.video_surface->opengl_tex.nonNull())
								{
									mat.albedo_texture = animtexdata.video_surface->opengl_tex;
									animtexdata.cur_frame_i++;
								}
							}
						}
						else // Else !USE_QT_TO_PLAY_MP4S, use WMFVideoReader to play mp4s:
						{
#if defined(_WIN32)
							if(animtexdata.video_reader.isNull() && !animtexdata.encounted_error) // If vid reader has not been created yet (and we haven't failed trying to create it):
							{
								if(animtexdata.create_vid_reader_task.isNull()) // If we have not created a CreateVidReaderTask yet:
								{
									ResourceRef resource = main_window->resource_manager->getExistingResourceForURL(mat.tex_path);
									
									// if the resource is downloaded already, read video off disk:
									std::string use_URL;
									if(resource.nonNull() && resource->getState() == Resource::State_Present)
									{
										use_URL = resource->getLocalPath();
									}
									else // Otherwise use streaming via HTTP
									{
										// If the URL does not have an HTTP prefix, rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
										if(!(hasPrefix(mat.tex_path, "http") || hasPrefix(mat.tex_path, "https")))
										{
											use_URL = "http://" + main_window->server_hostname + "/resource/" + web::Escaping::URLEscape(mat.tex_path);
										}
										else
											use_URL = mat.tex_path;
									}


									animtexdata.callback = new SubstrataVideoReaderCallback();
									animtexdata.callback->sample_queue = &animtexdata.sample_queue;

									// Create and launch a CreateVidReaderTask to create the video reader off the main thread.
									Reference<CreateVidReaderTask> create_vid_reader_task = new CreateVidReaderTask();
									create_vid_reader_task->callback = animtexdata.callback;
									create_vid_reader_task->URL = use_URL;
									create_vid_reader_task->dev_manager = main_window->device_manager.ptr;

									animtexdata.create_vid_reader_task = create_vid_reader_task;

									main_window->task_manager.addTask(create_vid_reader_task);
								}
								else // Else CreateVidReaderTask has been created already as is executing or has executed:
								{
									// See if the CreateVidReaderTask has completed:
									Reference<VideoReader> vid_reader;
									{
										Lock lock2(animtexdata.create_vid_reader_task->mutex);
										if(!animtexdata.create_vid_reader_task->error_msg.empty())
											throw glare::Exception(animtexdata.create_vid_reader_task->error_msg);
										vid_reader = animtexdata.create_vid_reader_task->vid_reader;
									}

									if(vid_reader.nonNull()) // If vid reader has been created by the CreateVidReaderTask:
									{
										animtexdata.create_vid_reader_task = Reference<CreateVidReaderTask>(); // Free CreateVidReaderTask.
										animtexdata.video_reader = vid_reader;
										animtexdata.in_anim_time = 0;
									}
								}
							}

							if(animtexdata.video_reader.nonNull())
							{
								WMFVideoReader* vid_reader = animtexdata.video_reader.downcastToPtr<WMFVideoReader>();

								animtexdata.in_anim_time += dt;
								const double in_anim_time = animtexdata.in_anim_time;
							
								/*
								Read frames from animtexdata.sample_queue queue. 
								If they are audio frames, copy audio data to audio source buffer.
								If they are vid frames, enqueue onto vid_frame_queue.
								We read ahead like this a bit so that we can get audio samples to the audio_source buffer ahead of time,
								to avoid stutters.
							

								Vid Reader ----------------------->   this code (AnimatedTextureManger) -------------------------> AudioEngine
										  animtexdata.sample_queue                    |                  ob->audio_source->buffer
																					  |
																					  |
																					   \
																						 ----------------------------------------->this code (AnimatedTextureManger) 
																								 animtexdata.vid_frame_queue
								*/

								Lock lock2(animtexdata.sample_queue.getMutex());
								while(animtexdata.sample_queue.unlockedNonEmpty())
								{
									SampleInfoRef sample;
									animtexdata.sample_queue.unlockedDequeue(sample);

									animtexdata.num_samples_pending--;
									//conPrint("Dequeued sample, num_samples_pending: " + toString(animtexdata.num_samples_pending));
									//if(frame.nonNull()) animtexdata.video_reader->startReadingNextFrame(); // We dequeued an actual frame, start reading a new frame.

									if(sample.nonNull())
									{
										if(sample->is_audio)
										{
											// Copy to our audio source
											if(ob->audio_source.nonNull())
											{
												// Convert to float data
												const uint32 bytes_per_sample  = sample->bits_per_sample / 8;
												const size_t num_sample_frames = sample->buffer_len_B / bytes_per_sample;
												const size_t num_samples = num_sample_frames / sample->num_channels;

												temp_buf.resize(num_samples);

												for(size_t z=0; z<num_samples; ++z)
												{
													const float left =  ((const int16*)sample->frame_buffer)[z*2 + 0] * (1.f / 32768.f);
													const float right = ((const int16*)sample->frame_buffer)[z*2 + 1] * (1.f / 32768.f);
													temp_buf[z] = (left + right) * 0.5f;
												}

												{
													Lock mutex(main_window->audio_engine.mutex);
													ob->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
												}
											}
										}
										else
										{
											animtexdata.vid_frame_queue.push_back(sample);
										}
									}
									else // Frame is null: we have reached EOS
									{
										animtexdata.at_vidreader_EOS = true;
									}
								}

								// Queue up some sample reads, so that the video decoder has decoded a few frames ahead of what we are showing currently.
								if(!animtexdata.at_vidreader_EOS) // If we reached EOS, don't queue up any more sample reads
								{
									const int target_num_vid_frames = 10;
									const int decoded_and_reading_vid_frames = (int)animtexdata.vid_frame_queue.size() + (int)vid_reader->num_pending_reads;
							
									//printVar(animtexdata.vid_frame_queue.size());
									//printVar(vid_reader->num_pending_reads);
									//printVar(decoded_and_reading_vid_frames);

									const int num_extra_reads = target_num_vid_frames - decoded_and_reading_vid_frames;
									for(int i=0; i<num_extra_reads; ++i)
									{
										vid_reader->startReadingNextSample();
										animtexdata.num_samples_pending++;
									}
								}


								// Is it time to display the next frame?
								// NOTE: cur_frame_i == -1 means we have seeked back to the beginning of the file (note we will still be displaying the last frame as current_video_frame)
								const bool need_new_vid_frame = (animtexdata.cur_frame_i == -1) || animtexdata.current_video_frame.isNull() || (in_anim_time >= (animtexdata.current_video_frame->frame_time + animtexdata.current_video_frame->frame_duration));

								//printVar(animtexdata.cur_frame_i);
								//printVar(animtexdata.in_anim_time);
								//if(animtexdata.current_video_frame.nonNull())
								//{
								//	printVar(animtexdata.current_video_frame->frame_time);
								//	printVar(animtexdata.current_video_frame->frame_duration);
								//}
								//printVar(need_new_vid_frame);

								bool got_new_vid_frame = false; // Did we get a video frame from vid_frame_queue?
								if(need_new_vid_frame && animtexdata.vid_frame_queue.nonEmpty())
								{
									animtexdata.current_video_frame = animtexdata.vid_frame_queue.front();
									animtexdata.vid_frame_queue.pop_front();

									assert(animtexdata.current_video_frame.nonNull()); // We only push non-null frames into queue.
									got_new_vid_frame = true;
								}

							
								if(got_new_vid_frame)
								{
									animtexdata.cur_frame_i++;
									//printVar(animtexdata.current_video_frame->frame_time);

									// Convert it to an OpenGL texture and apply to the current material
									WMFSampleInfo* wmf_cur_frame = animtexdata.current_video_frame.downcastToPtr<WMFSampleInfo>();

									assert(!wmf_cur_frame->is_audio);
								
									// Unlock previous OpenGL/d3d texture via locked_interop_tex_ob
									if(animtexdata.locked_interop_tex_ob)
									{
										BOOL res = main_window->wgl_funcs.wglDXUnlockObjectsNV(main_window->interop_device_handle, 1, &animtexdata.locked_interop_tex_ob);
										if(!res)
											conPrint("Warning: wglDXUnlockObjectsNV failed.");
										animtexdata.locked_interop_tex_ob = NULL;
										mat.albedo_texture = NULL;
									}

									// Direct3DUtils::saveTextureToBmp("frames/frame_" + toString(animtexdata.cur_frame_i) + ".bmp", wmf_cur_frame->d3d_tex.ptr);

									OpenGLAndD3DTex info;
									auto tex_res = animtexdata.opengl_tex_for_d3d_tex.find(wmf_cur_frame->d3d_tex.ptr);
									if(tex_res == animtexdata.opengl_tex_for_d3d_tex.end())
									{
										// Create an OpenGL texture that will correspond to the d3d texture
										OpenGLTextureRef opengl_tex = new OpenGLTexture();
										glGenTextures(1, &opengl_tex->texture_handle);
									
										//conPrint("making new OpenGL for d3d tex " + toHexString((uint64)wmf_cur_frame->d3d_tex.ptr) + ", texture_handle: " + toString(opengl_tex->texture_handle));
									
										// Unforuntately this doesn't work: (not supported by driver?)
										// const int GL_TEXTURE_FORMAT_SRGB_OVERRIDE_EXT = 0x8FBF;
										// glTexParameteri(opengl_tex->texture_handle, GL_TEXTURE_FORMAT_SRGB_OVERRIDE_EXT, GL_SRGB);
									
										const GLenum WGL_ACCESS_READ_ONLY_NV = 0x0;
										HANDLE interop_tex_ob = main_window->wgl_funcs.wglDXRegisterObjectNV(main_window->interop_device_handle, wmf_cur_frame->d3d_tex.ptr,
											opengl_tex->texture_handle, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
									
										info.opengl_tex = opengl_tex;
										info.interop_handle = interop_tex_ob;
										animtexdata.opengl_tex_for_d3d_tex[wmf_cur_frame->d3d_tex.ptr] = info;
									}
									else
										info = tex_res->second;

									if(info.interop_handle)
									{
										//conPrint("Locking interop_handle " + toString((uint64)info.interop_handle));
										const BOOL res = main_window->wgl_funcs.wglDXLockObjectsNV(main_window->interop_device_handle, 1, &info.interop_handle);
										if(res)
										{
											animtexdata.locked_interop_tex_ob = info.interop_handle;
											mat.albedo_texture = info.opengl_tex; // Apply to material
										}
										else
										{
											conPrint("warning: failed to lock object");
										}
									}
								}

								// If the video reader has read all video frames, and signalled EOS, and there are no queued sample reads,
								// and we have no outstanding video frames to display, 
								// then we can seek back to the start.
								// Note that IMFSourceReader will return an error if we try to seek while there are sample reads queued.
								if(animtexdata.at_vidreader_EOS && (animtexdata.num_samples_pending == 0) && 
									animtexdata.vid_frame_queue.empty())
								{
									animtexdata.cur_frame_i = -1;
									animtexdata.in_anim_time = 0;
									
									//conPrint("Seeking to 0.0");
									vid_reader->seek(0.0);
									animtexdata.at_vidreader_EOS = false;
								}
							}
#endif // #if defined(_WIN32)
						} // End if !USE_QT_TO_PLAY_MP4S
					}
					catch(glare::Exception& e)
					{
						animtexdata.encounted_error = true;
						conPrint(e.what());
						main_window->showErrorNotification(e.what());
					}
				} // end if(in_process_dist)
			} // end if(hasExtensionStringView(mat.tex_path, "mp4"))
		} // end for each material
	} // end if(ob->opengl_engine_ob.nonNull())
}
