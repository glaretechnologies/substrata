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
#include <webserver/Escaping.h>
#include <utils/Base64.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FileInStream.h>
#include <utils/IncludeHalf.h>
#include <xxhash.h>
#include <tracy/Tracy.hpp>


AnimatedTexData::AnimatedTexData(bool is_mp4_)
:	is_mp4(is_mp4_)
{
}

AnimatedTexData::~AnimatedTexData()
{
	browser = NULL;
}


[[maybe_unused]] static std::string makeDataURL(const std::string& html)
{
	std::string html_base64;
	Base64::encode(html.data(), html.size(), html_base64);

	return "data:text/html;base64," + html_base64;
}


void AnimatedTexObData::processMP4AnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
	OpenGLMaterial& mat, AnimatedTexData& animtexdata, const OpenGLTextureKey& tex_path, bool is_refl_tex)
{
#if CEF_SUPPORT
	if(!CEF::isInitialised())
	{
		CEF::initialiseCEF(gui_client->base_dir_path);
	}

	if(CEF::isInitialised())
	{
		if(animtexdata.browser.isNull() && !tex_path.empty() && ob->opengl_engine_ob.nonNull())
		{
			gui_client->logMessage("Creating browser to play vid, URL: " + tex_path);

			const int width = 1024;
			const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
			const int height = myClamp((int)(1024 * use_height_over_width), 16, 2048);

			std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
			OpenGLTextureRef new_tex /*mat.albedo_texture*/ = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTextureFormat::Format_SRGBA_Uint8,
				GL_SRGB8_ALPHA8, // GL internal format
				GL_BGRA, // GL format.
				OpenGLTexture::Filtering_Bilinear);

			if(is_refl_tex)
			{
				mat.albedo_texture = new_tex;
				mat.fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
			}
			else
				mat.emission_texture = new_tex;

			opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);

			ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(tex_path);

			// if the resource is downloaded already, read video off disk:
			std::string use_URL;
			if(resource.nonNull() && resource->getState() == Resource::State_Present)
			{
				use_URL = "https://resource/" + web::Escaping::URLEscape(tex_path);// resource->getLocalPath();
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
					use_URL = "http://" + gui_client->server_hostname + "/resource/" + web::Escaping::URLEscape(tex_path);
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

			Reference<EmbeddedBrowser> browser = new EmbeddedBrowser();
			browser->create(data_URL, new_tex, gui_client, ob, opengl_engine);

			animtexdata.browser = browser;
		}
	}
#endif // CEF_SUPPORT
}


AnimatedTexObDataProcessStats AnimatedTexObData::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	ZoneScoped; // Tracy profiler

	AnimatedTexObDataProcessStats stats;
	stats.num_gif_textures_processed = 0;
	stats.num_mp4_textures_processed = 0;
	stats.num_gif_frames_advanced = 0;

	if(ob->opengl_engine_ob.isNull())
		return stats;

	const bool in_cam_frustum = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);

	// Work out if the object is sufficiently large, as seen from the camera, and sufficiently close to the camera.
	// Gifs will play further away than mp4s.
	bool large_enough;
	bool mp4_large_enough;
	{
		const float max_dist = 200.f; // textures <= max_dist are updated
		const float min_recip_dist = 1 / max_dist; // textures >= min_recip_dist are updated

		const float max_mp4_dist = (float)AnimatedTexData::maxVidPlayDist(); // textures <= max_dist are updated
		const float min_mp4_recip_dist = 1 / max_mp4_dist; // textures >= min_recip_dist are updated

		const float ob_w = ob->getAABBWSLongestLength();
		const float recip_dist = (ob->getCentroidWS() - gui_client->cam_controller.getPosition().toVec4fPoint()).fastApproxRecipLength();
		const float proj_len = ob_w * recip_dist;

		large_enough     = (proj_len > 0.01f) && (recip_dist > min_recip_dist);
		mp4_large_enough = (proj_len > 0.01f) && (recip_dist > min_mp4_recip_dist);
	}

	if(in_cam_frustum && large_enough)
	{
		AnimatedTexObData& animation_data = *this;
		animation_data.mat_animtexdata.resize(ob->opengl_engine_ob->materials.size());

		for(size_t m=0; m<ob->opengl_engine_ob->materials.size(); ++m)
		{
			OpenGLMaterial& mat = ob->opengl_engine_ob->materials[m];
			
			//---- Handle animated reflection texture ----
			AnimatedTexData* refl_data = animation_data.mat_animtexdata[m].refl_col_animated_tex_data.ptr();
			if(refl_data)
			{
				if(refl_data->is_mp4)
				{
					if(mp4_large_enough)
					{
						processMP4AnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, *refl_data, mat.tex_path, /*is refl tex=*/true);
						stats.num_mp4_textures_processed++;
					}
				}
				//else
				//{
				//	processGIFAnimatedTex(gui_client, opengl_engine, ob->opengl_engine_ob, ob, anim_time, dt, mat, mat.albedo_texture, *refl_data, mat.tex_path, /*is refl tex=*/true, stats.num_gif_frames_advanced);
				//	stats.num_gif_textures_processed++;
				//}
			}

			//---- Handle animated emission texture ----
			AnimatedTexData* emission_data = animation_data.mat_animtexdata[m].emission_col_animated_tex_data.ptr();
			if(emission_data)
			{
				if(emission_data->is_mp4)
				{
					if(mp4_large_enough)
					{
						processMP4AnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, *emission_data, mat.emission_tex_path, /*is refl tex=*/false);
						stats.num_mp4_textures_processed++;
					}
				}
				//else
				//{
				//	processGIFAnimatedTex(gui_client, opengl_engine, ob->opengl_engine_ob, ob, anim_time, dt, mat, mat.emission_texture, *emission_data, mat.emission_tex_path, /*is refl tex=*/false, stats.num_gif_frames_advanced);
				//	stats.num_gif_textures_processed++;
				//}
			}
		}
	}

	// If the object is sufficiently far from the camera, clean up browser anim data.
	// Note that we only want to do this when the object is far away, not just when it moves outside the camera frustum.
	if(!mp4_large_enough)
	{
#if CEF_SUPPORT
		// Close any browsers for animated textures on this object.
		for(size_t m=0; m<this->mat_animtexdata.size(); ++m)
		{
			if(this->mat_animtexdata[m].refl_col_animated_tex_data.nonNull())
			{
				AnimatedTexData& animtexdata = *this->mat_animtexdata[m].refl_col_animated_tex_data;
				if(animtexdata.browser.nonNull())
				{
					gui_client->logMessage("Closing vid playback browser (out of view distance).");
					animtexdata.browser = NULL;

					// Remove audio source
					if(ob->audio_source.nonNull())
					{
						gui_client->audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
					}
				}
			}

			if(this->mat_animtexdata[m].emission_col_animated_tex_data.nonNull())
			{
				AnimatedTexData& animtexdata = *this->mat_animtexdata[m].emission_col_animated_tex_data;
				if(animtexdata.browser.nonNull())
				{
					gui_client->logMessage("Closing vid playback browser (out of view distance).");
					animtexdata.browser = NULL;

					// Remove audio source
					if(ob->audio_source.nonNull())
					{
						gui_client->audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
					}
				}
			}
		}
#endif
	}

	// If the object is sufficiently far from the camera, clean up gif playback data.
	// NOTE: nothing to do now tex_data for animated textures is stored in the texture?

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

		if(hasExtension(mat.tex_path, "mp4"))
		{
			if(animation_data.mat_animtexdata[m].refl_col_animated_tex_data.isNull())
				animation_data.mat_animtexdata[m].refl_col_animated_tex_data = new AnimatedTexData(/*is mp4=*/true);
		}

		// Emission tex
		if(hasExtension(mat.emission_tex_path, "mp4"))
		{
			if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
				animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData(/*is mp4=*/true);
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
								else
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
