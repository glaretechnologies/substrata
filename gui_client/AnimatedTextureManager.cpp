/*=====================================================================
AnimatedTextureManager.cpp
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "AnimatedTextureManager.h"


#include "GUIClient.h"
#include "EmbeddedBrowser.h"
#include "LoadTextureTask.h"
#include "CEF.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <opengl/PBOPool.h>
#include <webserver/Escaping.h>
#include <video/WMFVideoReader.h>
#include <direct3d/Direct3DUtils.h>
#include <utils/Base64.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FileInStream.h>
#include <utils/IncludeHalf.h>
#include <xxhash.h>
#include <tracy/Tracy.hpp>
#ifdef _WIN32
#include <d3d11.h>
#include <d3d11_4.h>
#endif


AnimatedTexData::AnimatedTexData(size_t mat_index_, bool is_refl_tex_)
:	shared_handle(nullptr),
	error_occurred(false),
	mat_index(mat_index_),
	is_refl_tex(is_refl_tex_)
{
}

AnimatedTexData::~AnimatedTexData()
{}


[[maybe_unused]] static std::string makeDataURL(const std::string& html)
{
	std::string html_base64;
	Base64::encode(html.data(), html.size(), html_base64);

	return "data:text/html;base64," + html_base64;
}


#if USE_WMF_FOR_MP4_PLAYBACK
struct CreateWMFVideoReaderTask : public glare::Task
{
	void run(size_t /*thread_index*/) override
	{
		try
		{
			Reference<WMFVideoReader> video_reader = new WMFVideoReader(/*read from video device=*/false, /*just read audio=*/false, 
				/*URL=*/path_or_URL, dx_device_manager, /*decode to d3d tex=*/true);

			video_reader->startReadingNextSample();

			{
				Lock lock(mutex);
				video_reader_result = video_reader;
			}
		}
		catch(glare::Exception& e)
		{
			Lock lock(mutex);
			error_str = e.what();
		}
	}

	Mutex mutex;
	Reference<WMFVideoReader> video_reader_result;
	std::string error_str;

	IMFDXGIDeviceManager* dx_device_manager;
	std::string path_or_URL;
};
#endif


void AnimatedTexData::processMP4AnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, IMFDXGIDeviceManager* dx_device_manager, ID3D11Device* d3d_device, glare::TaskManager& task_manager, WorldObject* ob, 
	double anim_time, double dt, const OpenGLTextureKey& tex_path)
{
#if USE_WMF_FOR_MP4_PLAYBACK
	if(!video_reader && !tex_path.empty())
	{
		if(!error_occurred)
		{
			if(!create_vid_reader_task)
			{
				ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(tex_path);
				if(resource && resource->isPresent())
				{
					gui_client->logMessage("Creating WMFVideoReader to play vid, URL: " + std::string(tex_path));

					create_vid_reader_task = new CreateWMFVideoReaderTask();
					create_vid_reader_task->path_or_URL = gui_client->resource_manager->getLocalAbsPathForResource(*resource);
					create_vid_reader_task->dx_device_manager = dx_device_manager;

					task_manager.addTask(create_vid_reader_task);
				}
			}
			else
			{
				// create_vid_reader_task has already been created, see if it is finished:
				{
					Lock lock(create_vid_reader_task->mutex);
					if(!create_vid_reader_task->error_str.empty()) // If an error occurred:
					{
						gui_client->logMessage("Error while creating WMFVideoReader for URL '" + std::string(tex_path) + "': " + create_vid_reader_task->error_str);
						error_occurred = true;
					}
					else if(create_vid_reader_task->video_reader_result)
						video_reader = create_vid_reader_task->video_reader_result;
				}

				if(error_occurred || video_reader) // If the create_vid_reader_task was finished:
					create_vid_reader_task = nullptr;
			}
		}
	}

	if(video_reader)
	{
		// Process any decoded frames: either add to audio_frame_queue or video_frame_queue.
		{
			Lock lock(video_reader->frame_queue.getMutex());
			while(video_reader->frame_queue.unlockedNonEmpty())
			{
				Reference<SampleInfo> frame = video_reader->frame_queue.unlockedDequeue();
				if(frame->is_audio)
					video_reader->audio_frame_queue.push_back(frame);
				else
					video_reader->video_frame_queue.push_back(frame);
			}
		}

		// Process any audio frames we have queued: mix down to mono and append to the audio source buffer.
		while(video_reader->audio_frame_queue.nonEmpty())
		{
			Reference<SampleInfo> front_frame = video_reader->audio_frame_queue.popAndReturnFront();

			assert(front_frame->is_audio);
			WMFSampleInfo* wmf_frame = front_frame.downcastToPtr<WMFSampleInfo>();

			//conPrint("got audio frame, buffer size: " + toString(front_frame->buffer_len_B) + " B");
			const uint32 bytes_per_sample = front_frame->bits_per_sample / 8;
			runtimeCheck((bytes_per_sample > 0) && (front_frame->num_channels > 0)); // Avoid divide by zero
			const uint64 num_samples = front_frame->buffer_len_B / bytes_per_sample / front_frame->num_channels;

			if(!ob->audio_source && !BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED))
			{
				// Create audio source
				ob->audio_source = new glare::AudioSource();

				// Create a streaming audio source.
				ob->audio_source->type = glare::AudioSource::SourceType_Streaming;
				ob->audio_source->pos = ob->getCentroidWS();
				ob->audio_source->debugname = "animated tex: " + tex_path;
				ob->audio_source->sampling_rate = front_frame->sample_rate_hz;// gui_client->audio_engine.getSampleRate();
				ob->audio_source->volume = myClamp(ob->audio_volume, 0.f, 10.f);

				{
					Lock lock(gui_client->world_state->mutex);

					const Parcel* parcel = gui_client->world_state->getParcelPointIsIn(ob->pos);
					ob->audio_source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.
				}

				gui_client->audio_engine.addSource(ob->audio_source);
			}

			// Copy to our audio source
			if(ob->audio_source)
			{
				temp_buf.resizeNoCopy(num_samples);

				if(bytes_per_sample == 2)
				{
					const int16* const src_data = (const int16*)wmf_frame->frame_buffer;
					if(front_frame->num_channels == 1)
					{
						runtimeCheck(num_samples * sizeof(int16) <= wmf_frame->buffer_len_B);
						for(int z=0; z<num_samples; ++z)
							temp_buf[z] = (float)src_data[z] * (1.f / std::numeric_limits<int16>::max());
					}
					else if(front_frame->num_channels == 2)
					{
						runtimeCheck(num_samples * 2 * sizeof(int16) <= wmf_frame->buffer_len_B);
						for(int z=0; z<num_samples; ++z)
						{
							const float left  = (float)src_data[z*2 + 0] * (1.f / std::numeric_limits<int16>::max());
							const float right = (float)src_data[z*2 + 1] * (1.f / std::numeric_limits<int16>::max());
							temp_buf[z] = (left + right) * 0.5f;
						}
					}
					else
					{
						for(int z=0; z<num_samples; ++z)
							temp_buf[z] = 0.f;
					}
				}
				else
				{
					// TODO: handle other bytes per sample
					for(int z=0; z<num_samples; ++z)
						temp_buf[z] = 0.f;
				}

				ob->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
				// conPrint("Added " + toString(num_samples) + " samples, num samples in ob->audio_source->buffer: " + toString(ob->audio_source->buffer.size()));
			}
		}



		// Check the video frame queue to see if there is a frame due to be presented.
		if(video_reader->video_frame_queue.nonEmpty())
		{
			Reference<SampleInfo> front_frame = video_reader->video_frame_queue.front(); // Look at front video frame, but don't remove from queue yet.
			assert(!front_frame->is_audio);
			if(!front_frame->is_EOS_marker)
			{
				WMFSampleInfo* wmf_frame = front_frame.downcastToPtr<WMFSampleInfo>();

				// conPrint("frame_time: " + toString(front_frame->frame_time) + ", video_reader->timer: " + toString(video_reader->timer.elapsed()));
				if(front_frame->frame_time <= video_reader->timer.elapsed()) // If the play time has reached the frame presentation time, then present the frame:
				{
					video_reader->video_frame_queue.pop_front(); // Remove from queue

					// conPrint("Presenting frame with time " + toString(front_frame->frame_time));

					if(!texture_copy)
					{
						texture_copy = Direct3DUtils::copyTextureToNewShareableTexture(d3d_device, wmf_frame->d3d_tex);;
						shared_handle = Direct3DUtils::getSharedHandleForTexture(texture_copy);
					}
				
					//----------------- Do the texture copy -----------------
					Direct3DUtils::copyTextureToExistingShareableTexture(d3d_device, /*source=*/wmf_frame->d3d_tex, /*dest=*/texture_copy);


					//====================== Create an OpenGL texture to show the video ========================
					if(video_display_opengl_tex.isNull())
					{
						gl_mem_ob = new OpenGLMemoryObject();

						gl_mem_ob->importD3D11ImageFromHandle(shared_handle);

						{
							//OpenGLMemoryObjectLock mem_ob_lock(gl_mem_ob);

							video_display_opengl_tex = new OpenGLTexture(front_frame->width, front_frame->height, opengl_engine, ArrayRef<uint8>(), OpenGLTextureFormat::Format_SRGBA_Uint8,
								OpenGLTexture::Filtering_Bilinear, 
								(ob->object_type == WorldObject::ObjectType_Video) ? OpenGLTexture::Wrapping_Clamp : OpenGLTexture::Wrapping_Repeat, // Video objects should have the UV transform correct to show [0, 1], other objects may not, so need tiling.
								/*has mipmaps=*/false, -1, 0, gl_mem_ob);

							glTextureParameteri(video_display_opengl_tex->texture_handle, GL_TEXTURE_SWIZZLE_R, GL_BLUE);  // Final R = interpreted B (orig R)
							glTextureParameteri(video_display_opengl_tex->texture_handle, GL_TEXTURE_SWIZZLE_G, GL_GREEN); // Final G = interpreted G (orig G)
							glTextureParameteri(video_display_opengl_tex->texture_handle, GL_TEXTURE_SWIZZLE_B, GL_RED);   // Final B = interpreted R (orig B)
							glTextureParameteri(video_display_opengl_tex->texture_handle, GL_TEXTURE_SWIZZLE_A, GL_ONE);   // Final A = 1 (opaque)
						}

						if(is_refl_tex)
							ob->opengl_engine_ob->materials[mat_index].albedo_texture = video_display_opengl_tex;
						else
							ob->opengl_engine_ob->materials[mat_index].emission_texture = video_display_opengl_tex;
						ob->opengl_engine_ob->materials[mat_index].allow_alpha_test = false;
						opengl_engine->materialTextureChanged(*ob->opengl_engine_ob, ob->opengl_engine_ob->materials[mat_index]);
					}
				}
			}
			else // Else if frame is EOS marker:
			{
				video_reader->video_frame_queue.pop_front(); // Remove from queue

				if(BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP))
				{
					// conPrint("Received EOS, seeking to beginning...");
					video_reader->seekToStart(); // Resets timer as well
				}
			}
		} // End if(front_frame)

		// Start doing some new sample reads if needed.
		assert(video_reader->audio_frame_queue.empty()); // We should have processed all audio samples.
		const int TARGET_NUM_FRAMES_DECODED_OR_DECODING = 6;
		const int num_additional_samples_needed = myMax(0, TARGET_NUM_FRAMES_DECODED_OR_DECODING - (int)video_reader->video_frame_queue.size() - (int)video_reader->num_pending_reads);
		for(int i=0; i<num_additional_samples_needed; ++i)
			video_reader->startReadingNextSample();
	}


#else // else if !USE_WMF_FOR_MP4_PLAYBACK:

#if CEF_SUPPORT
	if(CEF::isInitialised())
	{
		if(browser.isNull() && !tex_path.empty())
		{
			gui_client->logMessage("Creating browser to play vid, URL: " + std::string(tex_path));

			const int width = 1024;
			const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
			const int height = myClamp((int)(1024 * use_height_over_width), 16, 2048);

			ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(tex_path);

			// if the resource is downloaded already, read video off disk:
			std::string use_URL;
			if(resource.nonNull() && resource->getState() == Resource::State_Present)
			{
				use_URL = "https://resource/" + web::Escaping::URLEscape(std::string(tex_path));// resource->getLocalPath();
			}
			else // Otherwise use streaming via HTTP
			{
				if(hasPrefix(tex_path, "http://") || hasPrefix(tex_path, "https://"))
				{
					use_URL = tex_path;
				}
				else
				{
					// If the URL does not have an HTTP prefix (e.g. is just a normal resource URL), rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
					use_URL = "http://" + gui_client->server_hostname + "/resource/" + web::Escaping::URLEscape(std::string(tex_path));
				}
			}

			// NOTE: We will use a custom HTML page with the loop attribute set to true.  Can also add 'controls' attribute to debug stuff.
			const std::string html =
				"<html>"
				"<head>"
				"</head>"
				"<body style=\"margin:0\">"
				"<video autoplay loop name=\"media\" id=\"thevid\" width=\"" + toString(width) + "px\" height=\"" + toString(height) + "px\">"
				"<source src=\"" + web::Escaping::HTMLEscape(use_URL) + "\" type=\"video/mp4\" />"
				"</video>"
				"</body>"
				"</html>";

			const std::string data_URL = makeDataURL(html);

			Reference<EmbeddedBrowser> new_browser = new EmbeddedBrowser();
			new_browser->create(data_URL, width, height, gui_client, ob, /*mat index=*/mat_index, /*apply_to_emission_texture=*/!is_refl_tex, opengl_engine);

			browser = new_browser;
		}
	}
#endif // CEF_SUPPORT

#endif // end if !USE_WMF_FOR_MP4_PLAYBACK
}


void AnimatedTexData::checkCloseMP4Playback(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob)
{
#if USE_WMF_FOR_MP4_PLAYBACK
	if(video_reader)
	{
		gui_client->logMessage("Closing video_reader (out of view distance).");

		gui_client->sendVideoReaderToGarbageDeleterThread(video_reader);
		video_reader = NULL;
		texture_copy = NULL;
		shared_handle = NULL;
		
		video_display_opengl_tex = NULL;
		gl_mem_ob = NULL;
		temp_buf.clearAndFreeMem();
		
		create_vid_reader_task = NULL;
		error_occurred = false;
		

		runtimeCheck(mat_index < ob->opengl_engine_ob->materials.size());
		if(is_refl_tex)
			ob->opengl_engine_ob->materials[mat_index].albedo_texture = nullptr;
		else
			ob->opengl_engine_ob->materials[mat_index].emission_texture = nullptr;
		opengl_engine->materialTextureChanged(*ob->opengl_engine_ob, ob->opengl_engine_ob->materials[mat_index]);


		// Remove audio source
		if(ob->audio_source)
		{
			gui_client->audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
		}
	}
#else // else if !USE_WMF_FOR_MP4_PLAYBACK:

#if CEF_SUPPORT
	// Close any browsers for animated textures on this object.
	if(browser)
	{
		gui_client->logMessage("Closing vid playback browser (out of view distance).");
		browser = NULL;

		// Remove audio source
		if(ob->audio_source.nonNull())
		{
			gui_client->audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
		}
	}
#endif // CEF_SUPPORT

#endif // end if !USE_WMF_FOR_MP4_PLAYBACK
}


AnimatedTexObDataProcessStats AnimatedTexObData::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, IMFDXGIDeviceManager* dx_device_manager, ID3D11Device* d3d_device, glare::TaskManager& task_manager, WorldObject* ob, double anim_time, double dt)
{
	ZoneScoped; // Tracy profiler

	AnimatedTexObDataProcessStats stats;
	stats.num_mp4_textures_processed = 0;

	if(ob->opengl_engine_ob.isNull())
		return stats;

	//const bool in_cam_frustum = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);

	// Work out if the object is sufficiently large, as seen from the camera, and sufficiently close to the camera.
	// Gifs will play further away than mp4s.
	bool mp4_large_enough;
	{
		const float max_mp4_dist = (float)AnimatedTexData::maxVidPlayDist(); // textures <= max_dist are updated
		const float min_mp4_recip_dist = 1 / max_mp4_dist; // textures >= min_recip_dist are updated

		const float ob_w = ob->getAABBWSLongestLength();
		const Vec3d cam_position = gui_client->cam_controller.getPosition();
		const float recip_dist = (ob->getCentroidWS() - cam_position.toVec4fPoint()).fastApproxRecipLength();
		const float proj_len = ob_w * recip_dist;

		mp4_large_enough = (proj_len > 0.01f) && (recip_dist > min_mp4_recip_dist);
	}

	if(/*in_cam_frustum && */mp4_large_enough)
	{
		this->mat_animtexdata.resize(ob->opengl_engine_ob->materials.size());

		for(size_t m=0; m<ob->opengl_engine_ob->materials.size(); ++m)
		{
			OpenGLMaterial& mat = ob->opengl_engine_ob->materials[m];
			
			//---- Handle animated reflection texture ----
			AnimatedTexData* refl_data = this->mat_animtexdata[m].refl_col_animated_tex_data.ptr();
			if(refl_data)
			{
				refl_data->processMP4AnimatedTex(gui_client, opengl_engine, dx_device_manager, d3d_device, task_manager, ob, anim_time, dt, mat.tex_path);
				stats.num_mp4_textures_processed++;
			}

			//---- Handle animated emission texture ----
			AnimatedTexData* emission_data = this->mat_animtexdata[m].emission_col_animated_tex_data.ptr();
			if(emission_data)
			{
				emission_data->processMP4AnimatedTex(gui_client, opengl_engine, dx_device_manager, d3d_device, task_manager, ob, anim_time, dt, mat.emission_tex_path);
				stats.num_mp4_textures_processed++;
			}
		}
	}

	// If the object is sufficiently far from the camera, clean up browser anim data.
	// Note that we only want to do this when the object is far away, not just when it moves outside the camera frustum.
	if(!mp4_large_enough)
	{
		// Close any video readers for animated textures on this object.
		for(size_t m=0; m<this->mat_animtexdata.size(); ++m)
		{
			if(this->mat_animtexdata[m].refl_col_animated_tex_data)
				this->mat_animtexdata[m].refl_col_animated_tex_data->checkCloseMP4Playback(gui_client, opengl_engine, ob);

			if(this->mat_animtexdata[m].emission_col_animated_tex_data)
				this->mat_animtexdata[m].emission_col_animated_tex_data->checkCloseMP4Playback(gui_client, opengl_engine, ob);
		}
	}

	return stats;
}


void AnimatedTexObData::rescanObjectForAnimatedTextures(OpenGLEngine* opengl_engine, WorldObject* ob, PCG32& rng, AnimatedTextureManager& animated_tex_manager)
{
	if(ob->opengl_engine_ob.isNull())
		return;

	AnimatedTexObData& animation_data = *this;
	animation_data.mat_animtexdata.resize(ob->opengl_engine_ob->materials.size());

	for(size_t m=0; m<ob->opengl_engine_ob->materials.size(); ++m)
	{
		const OpenGLMaterial& mat = ob->opengl_engine_ob->materials[m];

		// Reflection/albedo tex
		if(hasExtension(mat.tex_path, "mp4"))
		{
			if(animation_data.mat_animtexdata[m].refl_col_animated_tex_data.isNull())
				animation_data.mat_animtexdata[m].refl_col_animated_tex_data = new AnimatedTexData(/*mat index=*/m, /*is refl tex=*/true);
		}

		// Emission tex
		if(hasExtension(mat.emission_tex_path, "mp4"))
		{
			if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
				animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData(/*mat index=*/m, /*is refl tex=*/false);
		}
	}
}


AnimatedTextureManager::AnimatedTextureManager()
:	last_num_textures_visible_and_close(0)
{
}


void AnimatedTextureManager::think(GUIClient* gui_client, OpenGLEngine* opengl_engine, double anim_time, double dt)
{
	last_num_textures_visible_and_close = 0;

	for(auto info_it = tex_info.begin(); info_it != tex_info.end(); ++info_it)
	{
		AnimatedTexInfo& info = *info_it->second;

		if(info.original_tex)
		{
			// Iterate over all objects using this texture, see if any are visible and close enough to camera
			bool visible_and_close = false;

			for(auto it = info.uses.begin(); (it != info.uses.end()) && !visible_and_close; ++it)
			{
				const GLObject* ob = it->ob.ptr();
				if(opengl_engine->isObjectInCameraFrustum(*ob))
				{
					// Work out if the object is sufficiently large, as seen from the camera, and sufficiently close to the camera.
					// Gifs will play further away than mp4s.
					const float max_dist = 200.f; // textures <= max_dist are updated
					const float min_recip_dist = 1 / max_dist; // textures >= min_recip_dist are updated

					const float ob_w = ob->aabb_ws.longestLength();
					const float recip_dist = (ob->aabb_ws.centroid() - gui_client->cam_controller.getPosition().toVec4fPoint()).fastApproxRecipLength();
					const float proj_len = ob_w * recip_dist;

					const bool large_enough     = (proj_len > 0.01f) && (recip_dist > min_recip_dist);
					if(large_enough)
						visible_and_close = true;
				}
			}

			if(visible_and_close)
			{
				TextureData* texdata = info.original_tex->texture_data.ptr();
				if(texdata)
				{
					const int num_frames = (int)texdata->num_frames;
					const double total_anim_time = texdata->last_frame_end_time;
					if((num_frames > 0) && (total_anim_time > 0)) // Check !frames.empty() for back() calls below.
					{
						const double in_anim_time = Maths::doubleMod(anim_time/* + info.time_offset*/, total_anim_time);
						assert(in_anim_time >= 0);

						// Search for the current frame, setting animtexdata.cur_frame_i, for in_anim_time.
						// Note that in_anim_time may have increased just a little bit since last time process() was called, or it may have jumped a lot if object left and re-entered the camera view.

						if(info.cur_frame_i < 0 && info.cur_frame_i >= num_frames) // Make sure cur_frame_i is in bounds.
						{
							assert(false);
							info.cur_frame_i = 0;
						}

						// If frame durations are equal, we can skip the frame search stuff, and just compute the frame index directly.
						if(texdata->frame_durations_equal)
						{
							int index = (int)(in_anim_time * texdata->recip_frame_duration);
							assert(index >= 0);

							if(index >= num_frames)
								index = 0;

							info.cur_frame_i = index;
						}
						else
						{
							// Else frame end times are irregularly spaced.  
							// Start with a special case search, where we assume in_anim_time just increased a little bit, so that we are still on the same frame, or maybe the next frame.
							// If that doesn't find the correct frame, then fall back to a binary search over all frames to find the correct frame.

							// Is in_anim_time still in the time range of the current frame?  Note that we need to check both frame start and end times as in_anim_time may have decreased.
							// cur frame start time = prev frame end time, but be careful about wraparound.
							const double cur_frame_start_time = (info.cur_frame_i == 0) ? 0.0 : texdata->frame_end_times[info.cur_frame_i - 1];
							if(in_anim_time >= cur_frame_start_time && in_anim_time <= texdata->frame_end_times[info.cur_frame_i])
							{
								// animtexdata.cur_frame_i is unchanged.
								//conPrint("current frame is unchanged");
							}
							else
							{
								// See if in_anim_time is in the time range of the next frame
								double next_frame_start_time, next_frame_end_time;
								int next_frame_index;
								if(info.cur_frame_i == num_frames - 1)
								{
									next_frame_start_time = 0.0;
									next_frame_end_time = texdata->frame_end_times[0];
									next_frame_index = 0;
								}
								else
								{
									next_frame_start_time = texdata->frame_end_times[info.cur_frame_i];
									next_frame_end_time   = texdata->frame_end_times[info.cur_frame_i + 1];
									next_frame_index = info.cur_frame_i + 1;
								}

								if(in_anim_time >= next_frame_start_time && in_anim_time <= next_frame_end_time) // if in_anim_time is in the time range of the next frame:
								{
									info.cur_frame_i = next_frame_index;
									//conPrint("advancing to next frame");
								}
								else
								{
									//conPrint("Finding frame with binary search");

									// Else in_anim_time was not in current frame or next frame periods.
									// Do binary search for current frame.
									const auto res = std::lower_bound(texdata->frame_end_times.begin(), texdata->frame_end_times.end(), in_anim_time); // Get the position of the first frame_end_time >= in_anim_time.
									int index = (int)(res - texdata->frame_end_times.begin());
									assert(index >= 0);
									if(index >= num_frames)
										index = 0;

									info.cur_frame_i = index;
								}
							}
						}

						assert(info.cur_frame_i >= 0 && info.cur_frame_i < num_frames); // Should be in bounds

						if(info.cur_frame_i >= 0 && info.cur_frame_i < num_frames) // Make sure in bounds
						{
							if(info.cur_frame_i != info.last_loaded_frame_i) // If cur frame changed: (Avoid uploading the same frame multiple times in a row)
							{
								if(gui_client->opengl_upload_thread)
								{
									Reference<OpenGLTexture> next_tex = (info.next_tex_i == 0) ? info.original_tex : info.other_tex;
									Reference<OpenGLTexture> from_tex = (info.next_tex_i == 0) ? info.other_tex    : info.original_tex;

									// NOTE: next_tex will be null if this is the first time we have updated a frame for this animated texture.
									// In this case the texture will be created on the OpenGLUploadThread.

									// Don't load new data into the texture that is currently applied to objects.
									// This could happen if the opengl upload thread is slow uploading textures.
									if(info.next_tex_i != info.cur_displayed_tex_i)
									{
										if(from_tex)
										{
											//conPrint("Sending UploadTextureMessage for tex " + info.original_tex->key);
											UploadTextureMessage* msg = gui_client->opengl_upload_thread->allocUploadTextureMessage();
											msg->is_animated_texture_update = true;
											//msg->tex_params = from_tex->getTexParams(); TODO: set tex params from from_tex
											msg->texture_data = texdata;
											msg->old_tex = from_tex;
											msg->new_tex = next_tex;
											msg->frame_i = info.cur_frame_i;
											if(!next_tex)
												msg->tex_path = info.original_tex->key;

											gui_client->opengl_upload_thread->getMessageQueue().enqueue(msg);

											info.next_tex_i = (info.next_tex_i + 1) % 2;
										}
									}
									//else
									//	conPrint("Can't start loading into next tex i " + toString(info.next_tex_i) + ", is currently applied");
								}
								else if(gui_client->pbo_pool) // Else if we are doing async texture uploads (which use the PBOPool):
								{
									// Insert message into gui_client async_texture_loaded_messages_to_process, which will load the frame in an async manner on the main OpenGL context.
									// Note that when loading this way (as opposed to using a separate upload thread), we seem to be able to get away with just updating
									// the texture currently in use, without seeing any glitches from partially updated textures.  In other words we don't need to ping-pong between two textures.
									Reference<TextureLoadedThreadMessage> msg = gui_client->allocTextureLoadedThreadMessage();
									msg->texture_data = texdata;
									msg->existing_opengl_tex = info.original_tex;
									msg->load_into_frame_i = info.cur_frame_i;
										
									gui_client->async_texture_loaded_messages_to_process.push_back(msg);
								}
								else
								{
									// Else synchronously update the texture with the new frame data
									if(info.original_tex && info.original_tex->texture_data)
									{
										TextureLoading::loadIntoExistingOpenGLTexture(info.original_tex, *info.original_tex->texture_data, info.cur_frame_i);
									}
								}
								
								info.last_loaded_frame_i = info.cur_frame_i;
							}
						}
					}
				}

				last_num_textures_visible_and_close++;
			} // end if(visible_and_close)
		}
	}
}


std::string AnimatedTextureManager::diagnostics()
{
	std::string s = "----AnimatedTextureManager----\n";
	s += "last_num_textures_visible_and_close: " + toString(last_num_textures_visible_and_close) + "\n\n";
	for(auto info_it = tex_info.begin(); info_it != tex_info.end(); ++info_it)
	{
		AnimatedTexInfo& info = *info_it->second;

		if(info.original_tex)
			s += std::string(info.original_tex->key) + ": uses: " + toString(info.uses.size()) + "\n";
	}
	s += "----------------------------\n";
	return s;
}


void AnimatedTextureManager::doTextureSwap(OpenGLEngine* opengl_engine, const OpenGLTextureRef& old_tex, const OpenGLTextureRef& new_tex)
{
	assert(old_tex->key == new_tex->key);

	auto res = tex_info.find(old_tex->key);
	if(res != tex_info.end())
	{
		AnimatedTexInfo* info = res->second.ptr();

		for(auto it = info->uses.begin(); it != info->uses.end(); ++it)
		{
			const AnimatedTexUse& use = *it;

			OpenGLMaterial& mat = use.ob->materials[use.mat_index];

			if(mat.albedo_texture == old_tex)
				mat.albedo_texture = new_tex;
			if(mat.emission_texture == old_tex)
				mat.emission_texture = new_tex;

			opengl_engine->materialTextureChanged(*use.ob, mat);
		}

		// Handle case where the other texture was created in OpenGLUploadThread.
		if(!info->other_tex)
		{
			//assert(old_tex == info->original_tex);
			info->other_tex = new_tex;
		}

		info->cur_displayed_tex_i = (info->cur_displayed_tex_i + 1) % 2;
	}
}


void AnimatedTextureManager::checkAddTextureUse(const Reference<OpenGLTexture>& texture, GLObjectRef ob, size_t mat_index)
{
	Reference<AnimatedTexInfo> info;
	auto res = tex_info.find(texture->key);
	if(res == tex_info.end())
	{
		info = new AnimatedTexInfo();

		info->last_loaded_frame_i = -1;
		info->cur_frame_i = 0;
		info->original_tex = texture;

		info->cur_displayed_tex_i = 0;
		info->next_tex_i = 1;

		tex_info[texture->key] = info;
	}
	else
		info = res->second;


	AnimatedTexUse use;
	use.ob = ob;
	use.mat_index = mat_index;
	info->uses.insert(use);
}


void AnimatedTextureManager::removeTextureUse(const Reference<OpenGLTexture>& tex, GLObjectRef ob, size_t mat_index)
{
	auto res = tex_info.find(tex->key);
	if(res != tex_info.end())
	{
		AnimatedTexInfo* info = res->second.ptr();

		AnimatedTexUse use;
		use.ob = ob;
		use.mat_index = mat_index;

		info->uses.erase(use);
		if(info->uses.empty())
			tex_info.erase(res);
	}
}
