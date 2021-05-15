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
//#include <QtMultimedia/QAbstractVideoSurface>
//#include <QtMultimedia/QVideoSurfaceFormat>
//#include <QtMultimedia/QMediaPlayer>
#include "../direct3d/Direct3DUtils.h"


AnimatedTexData::AnimatedTexData()
:	media_player(NULL), cur_frame_i(0), in_anim_time(0), encounted_error(false), at_vidreader_EOS(false), num_samples_pending(0)
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
			Reference<WMFVideoReader> vid_reader_ = new WMFVideoReader(/*read from vid device=*/false, URL, callback, dev_manager, /*decode_to_d3d_tex=*/true);

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

	std::string URL;
	SubstrataVideoReaderCallback* callback;
	IMFDXGIDeviceManager* dev_manager;

	Mutex mutex; // protects vid_reader and error_msg
	Reference<VideoReader> vid_reader;
	std::string error_msg; // Set to a non-empty string on failure.
};



#if 0
class SubVideoSurface : public QAbstractVideoSurface
{
public:
	SubVideoSurface(QObject *parent)
	:	QAbstractVideoSurface(parent),
		image_format(QImage::Format_Invalid)
	{
	}

	virtual bool present(const QVideoFrame& frame) override
	{
		conPrint("SubVideoSurface: present()");

		const bool res = ((QVideoFrame&)frame).map(QAbstractVideoBuffer::ReadOnly);
		
		const uchar* bits = frame.bits();
		
		
		printVar(frame.width());
		printVar(frame.height());
		printVar(frame.bytesPerLine());
		printVar(frame.isMapped());
		printVar(frame.isReadable());
		
		opengl_tex->load(frame.width(), frame.height(), frame.bytesPerLine(),
			ArrayRef<uint8>(bits, frame.height() * frame.bytesPerLine()));
		
		((QVideoFrame&)frame).unmap();

		current_frame = frame;

		return true;
	}

	virtual bool start(const QVideoSurfaceFormat& format) override
	{
		conPrint("SubVideoSurface: start()");

		printVar(format.frameWidth());
		printVar(format.frameHeight());
		printVar(format.handleType());
		printVar(format.pixelFormat());

		/*switch(format.pixelFormat())
		{
		case QVideoFrame::Format_ARGB32:*/

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
			GL_RGBA, // GL internal format (num channels)
			GL_RGBA, // GL format.  Video frames are BGRA.
			OpenGLTexture::Filtering_Bilinear,
			OpenGLTexture::Wrapping_Repeat
		);
	
		image_format = QVideoFrame::imageFormatFromPixelFormat(format.pixelFormat());

		return QAbstractVideoSurface::start(format); // "Note: You must call the base class implementation of start() at the end of your implementation."
	}

	virtual void stop() override
	{
		conPrint("SubVideoSurface: stop()");

		current_frame = QVideoFrame();

		QAbstractVideoSurface::stop(); // "Note: You must call the base class implementation of stop() at the start of your implementation."
	}

	virtual QList<QVideoFrame::PixelFormat>	supportedPixelFormats(QAbstractVideoBuffer::HandleType handle_type) const override
	{
		/*QList<QVideoFrame::PixelFormat> formats;
		formats.push_back(QVideoFrame::Format_RGB24);
		formats.push_back(QVideoFrame::Format_ARGB32);
		formats.push_back(QVideoFrame::Format_RGB32);
		formats.push_back(QVideoFrame::Format_BGRA32);
		formats.push_back(QVideoFrame::Format_ABGR32);
		formats.push_back(QVideoFrame::Format_BGR32);
		formats.push_back(QVideoFrame::Format_BGR24);
		return formats;*/
		if (handle_type == QAbstractVideoBuffer::NoHandle) {
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
	QVideoFrame current_frame;
	QImage::Format image_format;
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
				const bool process = ob_dist_from_cam < 20.0; // Only play videos within X metres for now.
				if(process)
				{
					if(ob->audio_source.nonNull())
					{
						main_window->audio_engine.setSourcePosition(ob->audio_source, ob->opengl_engine_ob->aabb_ws.centroid());
					}

#if defined(_WIN32)
					try
					{
						// Qt MediaPlayer playback:
						/*if(animtexdata.media_player == NULL)
						{
							std::string use_URL = mat.tex_path;
							// If the URL does not have an HTTP prefix, rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
							if(!(hasPrefix(mat.tex_path, "http") || hasPrefix(mat.tex_path, "https")))
							{
								use_URL = "http://" + main_window->server_hostname + "/resource/" + web::Escaping::URLEscape(mat.tex_path);
							}

							animtexdata.video_surface = new SubVideoSurface(NULL);
							
							use_URL = "E:\\video\\zeus.mp4"; // TEMP

							animtexdata.media_player = new QMediaPlayer(NULL, QMediaPlayer::VideoSurface);
							animtexdata.media_player->setVideoOutput(animtexdata.video_surface);
							animtexdata.media_player->setMedia(QUrl::fromLocalFile("E:\\video\\busted.mp4"));// QUrl(QtUtils::toQString(use_URL)));
							animtexdata.media_player->play();

							//VideoPlayer* player = new VideoPlayer();
							//player->show();
						}
						else
						{
							if(animtexdata.video_surface->opengl_tex.nonNull() && animtexdata.video_surface->current_frame.isValid())
							{
								mat.albedo_texture = animtexdata.video_surface->opengl_tex;

								QVideoFrame& frame = animtexdata.video_surface->current_frame;
								const bool res = frame.map(QAbstractVideoBuffer::ReadOnly);
							
								const uchar* bits = frame.bits();
							
								printVar(frame.width());
								printVar(frame.height());
								printVar(frame.bytesPerLine(0));
							
								//mat.albedo_texture->load(frame.width(), frame.height(), frame.bytesPerLine(0),
								//	ArrayRef<uint8>(bits, frame.height() * frame.bytesPerLine(0)));

								QImage image(
									frame.bits(),
									frame.width(),
									frame.height(),
									frame.bytesPerLine(),
									animtexdata.video_surface->image_format);

								//bool save_res = image.save(QtUtils::toQString("frame_" + toString(animtexdata.cur_frame_i) + ".png"));
								//assert(save_res);

								animtexdata.cur_frame_i++;
							
								frame.unmap();
							}
						}*/

						if(animtexdata.video_reader.isNull() && !animtexdata.encounted_error) // If vid reader has not been created yet (and we haven't failed trying to create it):
						{
							if(animtexdata.create_vid_reader_task.isNull()) // If we have not created a CreateVidReaderTask yet:
							{
								animtexdata.callback = new SubstrataVideoReaderCallback();
								animtexdata.callback->sample_queue = &animtexdata.sample_queue;
							
								std::string use_URL = mat.tex_path;
								// If the URL does not have an HTTP prefix, rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
								if(!(hasPrefix(mat.tex_path, "http") || hasPrefix(mat.tex_path, "https")))
								{
									use_URL = "http://" + main_window->server_hostname + "/resource/" + web::Escaping::URLEscape(mat.tex_path);
								}
							
								// conPrint("Creating video reader with URL '" + use_URL + "'...");
							
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
					}
					catch(glare::Exception& e)
					{
						animtexdata.encounted_error = true;
						conPrint(e.what());
						main_window->showErrorNotification(e.what());
					}
#endif // #if defined(_WIN32)
				} // end if(process)
			} // end if(hasExtensionStringView(mat.tex_path, "mp4"))
		}
	}
}
