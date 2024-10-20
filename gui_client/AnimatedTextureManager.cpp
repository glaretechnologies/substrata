/*=====================================================================
AnimatedTextureManager.cpp
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "AnimatedTextureManager.h"


#include "GUIClient.h"
#include "EmbeddedBrowser.h"
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
#include <xxhash.h>


AnimatedTexData::AnimatedTexData()
:	last_loaded_frame_i(-1),
	cur_frame_i(0)
{}

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


void AnimatedTexObData::processGIFAnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
	OpenGLMaterial& mat, Reference<OpenGLTexture>& texture, AnimatedTexData& animtexdata, const std::string& tex_path, bool is_refl_tex)
{
	TextureData* texdata = texture->texture_data.ptr();
	if(texdata)
	{
		const int num_frames = (int)texdata->num_frames;
		const double total_anim_time = texdata->last_frame_end_time;
		if((num_frames > 0) && (total_anim_time > 0)) // Check !frames.empty() for back() calls below.
		{
			const double in_anim_time = Maths::doubleMod(anim_time, total_anim_time);
			assert(in_anim_time >= 0);

			// Search for the current frame, setting animtexdata.cur_frame_i, for in_anim_time.
			// Note that in_anim_time may have increased just a little bit since last time process() was called, or it may have jumped a lot if object left and re-entered the camera view.

			if(animtexdata.cur_frame_i < 0 && animtexdata.cur_frame_i >= num_frames) // Make sure cur_frame_i is in bounds.
			{
				assert(false);
				animtexdata.cur_frame_i = 0;
			}

			// If frame durations are equal, we can skip the frame search stuff, and just compute the frame index directly.
			if(texdata->frame_durations_equal)
			{
				int index = (int)(in_anim_time * texdata->recip_frame_duration);
				assert(index >= 0);

				if(index >= num_frames)
					index = 0;

				animtexdata.cur_frame_i = index;
			}
			else
			{
				// Else frame end times are irregularly spaced.  
				// Start with a special case search, where we assume in_anim_time just increased a little bit, so that we are still on the same frame, or maybe the next frame.
				// If that doesn't find the correct frame, then fall back to a binary search over all frames to find the correct frame.

				// Is in_anim_time still in the time range of the current frame?  Note that we need to check both frame start and end times as in_anim_time may have decreased.
				// cur frame start time = prev frame end time, but be careful about wraparound.
				const double cur_frame_start_time = (animtexdata.cur_frame_i == 0) ? 0.0 : texdata->frame_end_times[animtexdata.cur_frame_i - 1];
				if(in_anim_time >= cur_frame_start_time && in_anim_time <= texdata->frame_end_times[animtexdata.cur_frame_i])
				{
					// animtexdata.cur_frame_i is unchanged.
					//conPrint("current frame is unchanged");
				}
				else
				{
					// See if in_anim_time is in the time range of the next frame
					double next_frame_start_time, next_frame_end_time;
					int next_frame_index;
					if(animtexdata.cur_frame_i == num_frames - 1)
					{
						next_frame_start_time = 0.0;
						next_frame_end_time = texdata->frame_end_times[0];
						next_frame_index = 0;
					}
					else
					{
						next_frame_start_time = texdata->frame_end_times[animtexdata.cur_frame_i];
						next_frame_end_time   = texdata->frame_end_times[animtexdata.cur_frame_i + 1];
						next_frame_index = animtexdata.cur_frame_i + 1;
					}

					if(in_anim_time >= next_frame_start_time && in_anim_time <= next_frame_end_time) // if in_anim_time is in the time range of the next frame:
					{
						animtexdata.cur_frame_i = next_frame_index;
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

						animtexdata.cur_frame_i = index;
					}
				}
			}

			assert(animtexdata.cur_frame_i >= 0 && animtexdata.cur_frame_i < num_frames); // Should be in bounds

			if(animtexdata.cur_frame_i >= 0 && animtexdata.cur_frame_i < num_frames) // Make sure in bounds
			{
				if(animtexdata.cur_frame_i != animtexdata.last_loaded_frame_i) // If cur frame changed: (Avoid uploading the same frame multiple times in a row)
				{
					/*printVar(animtexdata.cur_frame_i);*/

					// There may be some frames after a LOD level change where the new texture with the updated size is loading, and thus we have a size mismatch.
					// Don't try and upload the wrong size or we will get an OpenGL error or crash.

					OpenGLTextureRef tex = is_refl_tex ? mat.albedo_texture : mat.emission_texture;

					if(tex.nonNull() && tex->xRes() == texdata->W && tex->yRes() == texdata->H)
						TextureLoading::loadIntoExistingOpenGLTexture(tex, *texdata, animtexdata.cur_frame_i);
					//else
					//	conPrint("AnimatedTexObData::process(): tex data W or H wrong.");

					animtexdata.last_loaded_frame_i = animtexdata.cur_frame_i;
				}
			}
		}
	}
}


void AnimatedTexObData::processMP4AnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
	OpenGLMaterial& mat, AnimatedTexData& animtexdata, const std::string& tex_path, bool is_refl_tex)
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
	AnimatedTexObDataProcessStats stats;
	stats.num_gif_textures_processed = 0;
	stats.num_mp4_textures_processed = 0;

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
			if(mat.albedo_texture.nonNull() && hasExtensionStringView(mat.tex_path, "gif"))
			{
				if(animation_data.mat_animtexdata[m].refl_col_animated_tex_data.isNull())
					animation_data.mat_animtexdata[m].refl_col_animated_tex_data = new AnimatedTexData();

				processGIFAnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, mat.albedo_texture, *animation_data.mat_animtexdata[m].refl_col_animated_tex_data, mat.tex_path, /*is refl tex=*/true);
				stats.num_gif_textures_processed++;
			}
			else if(hasExtensionStringView(mat.tex_path, "mp4"))
			{
				if(mp4_large_enough)
				{
					if(animation_data.mat_animtexdata[m].refl_col_animated_tex_data.isNull())
						animation_data.mat_animtexdata[m].refl_col_animated_tex_data = new AnimatedTexData();

					processMP4AnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, *animation_data.mat_animtexdata[m].refl_col_animated_tex_data, mat.tex_path, /*is refl tex=*/true);
					stats.num_mp4_textures_processed++;
				}
			}

			//---- Handle animated emission texture ----
			if(mat.emission_texture.nonNull() && hasExtensionStringView(mat.emission_tex_path, "gif"))
			{
				if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
					animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData();

				processGIFAnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, mat.emission_texture, *animation_data.mat_animtexdata[m].emission_col_animated_tex_data, mat.emission_tex_path, /*is refl tex=*/false);
				stats.num_gif_textures_processed++;

			}
			else if(hasExtensionStringView(mat.emission_tex_path, "mp4"))
			{
				if(mp4_large_enough)
				{
					if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
						animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData();

					processMP4AnimatedTex(gui_client, opengl_engine, ob, anim_time, dt, mat, *animation_data.mat_animtexdata[m].emission_col_animated_tex_data, mat.emission_tex_path, /*is refl tex=*/false);
					stats.num_mp4_textures_processed++;
				}
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
