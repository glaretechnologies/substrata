/*=====================================================================
AnimatedTextureManager.cpp
--------------------------
Copyright Glare Technologies Limited 2021-
=====================================================================*/
#include "AnimatedTextureManager.h"


#include "MainWindow.h"
#include "../shared/WorldObject.h"
#include "../video/WMFVideoReader.h"
#include "Escaping.h"


AnimatedTexData::AnimatedTexData()
:	cur_frame_i(0), latest_tex_index(0), in_anim_time(0), encounted_error(false)
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
	virtual void frameDecoded(VideoReader* vid_reader, const FrameInfoRef& frameinfo)
	{
		//conPrint("frameDecoded, time " + toString(frameinfo.frame_time));

		frameinfos->enqueue(frameinfo);
	}


	virtual void endOfStream(VideoReader* vid_reader)
	{
		//conPrint("endOfStream()");

		// insert an empty FrameInfo to signify EOS
		frameinfos->enqueue(NULL);
	}

	ThreadSafeQueue<FrameInfoRef>* frameinfos;
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



void AnimatedTexObData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	if(ob->opengl_engine_ob.nonNull())
	{
		//ui->glWidget->makeCurrent();

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
#if defined(_WIN32)
					try
					{
						if(animtexdata.video_reader.isNull() && !animtexdata.encounted_error) // If vid reader has not been created yet (and we haven't failed trying to create it):
						{
							if(animtexdata.create_vid_reader_task.isNull()) // If we have not created a CreateVidReaderTask yet:
							{
								animtexdata.callback = new SubstrataVideoReaderCallback();
								animtexdata.callback->frameinfos = &animtexdata.frameinfos;

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

									for(int i=0; i<5; ++i)
										animtexdata.video_reader->startReadingNextFrame();
								}
							}
						}

						if(animtexdata.video_reader.nonNull())
						{
							WMFVideoReader* vid_reader = animtexdata.video_reader.downcastToPtr<WMFVideoReader>();

							animtexdata.in_anim_time += dt;
							const double in_anim_time = animtexdata.in_anim_time;


							// If we do not have a next_frame, try and get one from the frame queue.
							// We may get a null frame ref, which signifies end of stream.
							bool EOS = false;
							if(animtexdata.next_frame.isNull())
							{
								size_t queue_size;
								{
									Lock lock2(animtexdata.frameinfos.getMutex());
									if(animtexdata.frameinfos.unlockedNonEmpty())
									{
										animtexdata.frameinfos.unlockedDequeue(animtexdata.next_frame);
										EOS = animtexdata.next_frame.isNull();
									}
									queue_size = animtexdata.frameinfos.size();
								}
							}

							if(animtexdata.next_frame.nonNull() && in_anim_time >= animtexdata.next_frame->frame_time) // If it's time to start displaying next_frame, and we have one:
							{
								animtexdata.cur_frame_i++;
								animtexdata.video_reader->startReadingNextFrame(); // We are consuming a frame, so queue up a frame read.

																				   // Unlock previous OpenGL/d3d texture via locked_interop_tex_ob
								if(animtexdata.locked_interop_tex_ob)
								{
									BOOL res = main_window->wgl_funcs.wglDXUnlockObjectsNV(main_window->interop_device_handle, 1, &animtexdata.locked_interop_tex_ob);
									if(!res)
										conPrint("Warning: wglDXUnlockObjectsNV failed.");
									animtexdata.locked_interop_tex_ob = NULL;
									mat.albedo_texture = NULL;
								}

								animtexdata.current_frame = NULL; // Free current frame

																  // Update current frame to be next_frame
								animtexdata.current_frame = animtexdata.next_frame;
								animtexdata.next_frame = NULL;

								// Convert it to an OpenGL texture and apply to the current material
								WMFFrameInfo* wmf_cur_frame = animtexdata.current_frame.downcastToPtr<WMFFrameInfo>();

								OpenGLAndD3DTex info;
								auto tex_res = animtexdata.opengl_tex_for_d3d_tex.find(wmf_cur_frame->d3d_tex.ptr);
								if(tex_res == animtexdata.opengl_tex_for_d3d_tex.end())
								{
									// Create an OpenGL texture that will correspond to the d3d texture
									OpenGLTextureRef opengl_tex = new OpenGLTexture();
									glGenTextures(1, &opengl_tex->texture_handle);

									conPrint("making new OpenGL for d3d tex " + toHexString((uint64)wmf_cur_frame->d3d_tex.ptr) + ", texture_handle: " + toString(opengl_tex->texture_handle));

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

							if(EOS)
							{
								// This was not an actual frame we received, but an end-of-stream sentinel value.
								// Note that we can get a few of these in a row, but we need to call seek just once, or we will get MF errors.
								// So we will seek iff cur_frame_i is != 0.
								if(animtexdata.cur_frame_i != 0)
								{
									// Seek to start of vid.
									animtexdata.cur_frame_i = 0;
									animtexdata.in_anim_time = 0;

									conPrint("Seeking to 0.0");
									vid_reader->seek(0.0);

									for(int i=0; i<5; ++i)
										animtexdata.video_reader->startReadingNextFrame();
								}
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
