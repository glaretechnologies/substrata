/*=====================================================================
BrowserVidPlayer.cpp
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "BrowserVidPlayer.h"


#include "GUIClient.h"
#include "UIEvents.h"
#include "EmbeddedBrowser.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "CEF.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <networking/URL.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/Base64.h>
#include "superluminal/PerformanceAPI.h"


#define GL_BGRA                           0x80E1


BrowserVidPlayer::BrowserVidPlayer()
:	browser(NULL),
	previous_is_visible(true),
	state(State_Unloaded)
{}


BrowserVidPlayer::~BrowserVidPlayer()
{
	browser = NULL;
}


// See https://webapps.stackexchange.com/questions/54443/format-for-id-of-youtube-video
static void checkYouTubeVideoID(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(isAlphaNumeric(s[i]) || s[i] == '-' || s[i] == '_'))
			throw glare::Exception("invalid char in youtube vid id: '" + std::string(1, s[i]) + "'");
}


// For youtube URLs, we want to transform the URL into an embedded webpage, and encode it into a data URL.
// Throws glare::Exception on failure.
static std::string makeEmbedHTMLForVideoURL(const std::string& video_url, int width, int height, WorldObject* ob, ResourceManager& resource_manager, const std::string& server_hostname)
{
	const URL parsed_URL = URL::parseURL(video_url);

	if(parsed_URL.scheme == "http" || parsed_URL.scheme == "https")
	{
		if(parsed_URL.host == "www.youtube.com" || parsed_URL.host == "youtu.be")
		{
			std::string video_id;
			if(parsed_URL.host == "www.youtube.com")
			{
				// Parse query
				const std::map<std::string, std::string> params = URL::parseQuery(parsed_URL.query);
				const auto res = params.find("v"); // Find "v" param (video id)
				if(res != params.end())
					video_id = res->second;
				else
					throw glare::Exception("Could not find video id param (v) in YouTube URL.");
			}
			else
			{
				// Handle URLs of the form https://youtu.be/SaejB5NSzOs
				assert(parsed_URL.host == "youtu.be");
				video_id = eatSuffix(eatPrefix(parsed_URL.path, "/"), "/");
			}

			checkYouTubeVideoID(video_id); // Check video id so we don't need to worry about escaping it

			const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
			const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
			const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

			// See https://developers.google.com/youtube/player_parameters
			const std::string player_params =
				"{												\n"
				"	height: '" + toString(height) + "',			\n"
				"	width: '" + toString(width) + "',			\n"
				"	videoId: '" + video_id + "',				\n"
				"	playerVars: {								\n"
				"		'playsinline': 1,						\n"
				"		'autoplay': " + toString(autoplay ? 1 : 0) + ",		\n"
				"		'loop': "     + toString(loop ? 1 : 0) + ",			\n"
				"		'playlist': '" + video_id + "'			\n"// Set the playlist to the video ID.  This is needed for looping to work.
				"	},											\n"
				"	events: {									\n"
				"		onReady: function(e) {					\n"
				"			" + (muted ? "e.target.mute();" : "") + "\n" // If video should be muted, insert a call to mute the video here.
				"		}										\n"
				"	}											\n"
				"}";

			// See https://developers.google.com/youtube/iframe_api_reference
			const std::string embed_html = 
				"<html>																			\n"
				"	<body style='margin:0px;'>													\n"
				"	<div id=\"player\"></div>													\n"
				"		<script>																\n"
				"			// Load the IFrame Player API code asynchronously.					\n"
				"			var tag = document.createElement('script');							\n"
				"			tag.src = \"https://www.youtube.com/iframe_api\";					\n"
				"			var firstScriptTag = document.getElementsByTagName('script')[0];	\n"
				"			firstScriptTag.parentNode.insertBefore(tag, firstScriptTag);		\n"
				"																				\n"
				"			// Replace the 'ytplayer' element with an <iframe> and				\n"
				"			// YouTube player after the API code downloads.						\n"
				"			var player;															\n"
				"			function onYouTubeIframeAPIReady() {								\n"
				"				player = new YT.Player('player', 								\n"
				+ player_params +
				"			)}																	\n"
				"		</script>																\n"
				"	</body>																		\n"
				"</html>																		\n";
				
			return embed_html;
		}
		else if(parsed_URL.host == "www.twitch.tv")
		{
			std::string channel_name = parsed_URL.path;
			channel_name = eatPrefix(channel_name, "/");

			const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
			const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

			// See https://dev.twitch.tv/docs/embed/video-and-clips/

			const std::string iframe_src_url = "https://player.twitch.tv/?channel=" + web::Escaping::URLEscape(channel_name) + "&parent=localdomain&autoplay=" + boolToString(autoplay) + "&muted=" + boolToString(muted);

			const std::string embed_html = 
				"<html>																					\n"
				"	<body style='margin:0px;'>															\n"
				"		<iframe style='margin:0px;'													\n"
				"			src=\"" + iframe_src_url + "\"												\n"
				"			height=" + toString(height - 5) + "											\n"
				"			width=" + toString(width - 5) + "											\n"
				"		allowfullscreen>																\n"
				"		</iframe>																		\n"
				"	</body>																				\n"
				"</html>																				\n";

			return embed_html;
		}
		else
			throw glare::Exception("only YouTube and Twitch HTTP URLS accepted currently.");
	}
	else // Else non-http:
	{
		ResourceRef resource = resource_manager.getExistingResourceForURL(video_url);

		// if the resource is downloaded already, read video off disk:
		std::string use_URL;
		if(resource.nonNull() && resource->getState() == Resource::State_Present)
		{
			use_URL = "https://resource/" + web::Escaping::URLEscape(video_url);// resource->getLocalPath();
		}
		else // Otherwise use streaming via HTTP
		{
			// Rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
			use_URL = "http://" + server_hostname + "/resource/" + web::Escaping::URLEscape(video_url);
		}

		const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
		const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
		const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

		const std::string attributes = "controls " + std::string(autoplay ? "autoplay " : "") + std::string(loop ? "loop " : "") + std::string(muted ? "muted " : "");

		// NOTE: We will use a custom HTML page with the loop attribute set to true.  Can also add 'controls' attribute to debug stuff.
		const std::string html =
			"<html>"
			"	<head>"
			"	</head>"
			"	<body style=\"margin:0\">"
			"		<video " + attributes + " name=\"media\" id=\"thevid\" width=\"" + toString(width) + "px\" height=\"" + toString(height) + "px\">"
			"			<source src=\"" + web::Escaping::HTMLEscape(use_URL) + "\" type=\"video/mp4\" />"
			"		</video>"
			"	</body>"
			"</html>";

		return html;
	}
}


static void getVidTextureDimensions(const std::string& video_URL, WorldObject* ob, int& width_out, int& height_out)
{
	const URL parsed_URL = URL::parseURL(video_URL);

	const bool is_http_URL = parsed_URL.scheme == "http" || parsed_URL.scheme == "https";

	if(!is_http_URL)
	{
		width_out = 1024;
		const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
		height_out = myClamp((int)(1024 * use_height_over_width), 16, 2048);
	}
	else
	{
		// Use standard YouTube embed aspect ratio.  See also MainWindow::on_actionAdd_Video_triggered().
		width_out = 1024;
		height_out = (int)(1024.f / 1920.f * 1080.f);
	}
}


void BrowserVidPlayer::createNewBrowserPlayer(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob)
{
	if(ob->materials.empty())
		throw glare::Exception("materials were empty");

	const std::string& video_URL = ob->materials[0]->emission_texture_url;

	int width, height;
	getVidTextureDimensions(video_URL, ob, width, height);
	

	if(!CEF::isInitialised())
		CEF::initialiseCEF(gui_client->base_dir_path);
	if(!CEF::isInitialised())
		throw glare::Exception("CEF could not be initialised");

	
	assert(browser.isNull());
	browser = NULL;

	if(video_URL.empty())
		throw glare::Exception("video_URL was empty");

	if(ob->opengl_engine_ob.isNull())
		throw glare::Exception("ob->opengl_engine_ob.isNull");

	gui_client->logMessage("Creating vid player browser, video_URL: " + video_URL);

	// Try and make page now, before we allocate a texture.
	const std::string root_page = makeEmbedHTMLForVideoURL(video_URL, width, height, ob, *gui_client->resource_manager, gui_client->server_hostname);

	gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

	std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
	ob->opengl_engine_ob->materials[0].emission_texture = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTexture::Format_SRGBA_Uint8,
		GL_SRGB8_ALPHA8, // GL internal format
		GL_BGRA, // GL format.
		OpenGLTexture::Filtering_Bilinear);

	//ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.

	opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);

	browser = new EmbeddedBrowser();

	const std::string use_url = "https://localdomain/"; // URL to serve the root page

	browser->create(use_url, ob->opengl_engine_ob->materials[0].emission_texture, gui_client, ob, opengl_engine, root_page);

	this->loaded_video_url = video_URL;
}


void BrowserVidPlayer::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

#if CEF_SUPPORT

	const double ob_dist_from_cam = ob->pos.getDist(gui_client->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	const bool in_process_dist = ob_dist_from_cam < max_play_dist;
	if(in_process_dist)
	{
		if(state == State_Unloaded)
		{
			try
			{
				createNewBrowserPlayer(gui_client, opengl_engine, ob);
				this->state = State_BrowserCreated;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error occured while created browser video player: " + e.what());
				gui_client->print("Error occured while created browser video player: " + e.what());
				this->state = State_ErrorOccurred;
			}
		}
		
		// While the webview is not visible by the camera, we discard any dirty-rect updates.  
		// When the webview becomes visible again, if there were any discarded dirty rects, then we know our buffer is out of date.
		// So invalidate the whole buffer.
		// Note that CEF only allows us to invalidate the whole buffer - see https://magpcss.org/ceforum/viewtopic.php?f=6&t=15079
		// If we could invalidate part of it, then we can maintain the actual discarded dirty rectangles (or a bounding rectangle around them)
		// and use that to just invalidate the dirty part.
		if(browser.nonNull())
		{
			const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
			if(ob_visible && !previous_is_visible) // If webview just became visible:
			{
				//conPrint("Browser became visible!");
				browser->browserBecameVisible();
			}

			previous_is_visible = ob_visible;
		}
	}
	else // else if !in_process_dist:
	{
		// If there is a browser, destroy it.
		if(state == State_BrowserCreated)
		{
			if(browser.nonNull())
			{
				if(!ob->materials.empty())
				{
					const std::string& video_URL = ob->materials[0]->emission_texture_url;
					gui_client->logMessage("Closing vid player browser (out of view distance), video_URL: " + video_URL);
				}
				browser = NULL;

				// Remove audio source
				if(ob->audio_source.nonNull())
				{
					gui_client->audio_engine.removeSource(ob->audio_source);
					ob->audio_source = NULL;
				}
			}
			this->state = State_Unloaded;
		}
	}
#endif // CEF_SUPPORT
}


void BrowserVidPlayer::videoURLMayHaveChanged(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob)
{
	if(ob->materials.empty())
		return;

	const std::string& video_URL = ob->materials[0]->emission_texture_url;
	if(video_URL != this->loaded_video_url)
	{
		if(browser.isNull())
		{
			try
			{
				createNewBrowserPlayer(gui_client, opengl_engine, ob);
				this->state = State_BrowserCreated;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error occured while created browser video player: " + e.what());
				gui_client->print("Error occured while created browser video player: " + e.what());
				this->state = State_ErrorOccurred;
			}
		}
		else
		{
			int width, height;
			getVidTextureDimensions(video_URL, ob, width, height);

			try
			{
				const std::string root_page = makeEmbedHTMLForVideoURL(video_URL, width, height, ob, *gui_client->resource_manager, gui_client->server_hostname);

				browser->updateRootPage(root_page);

				const std::string use_url = "https://localdomain/"; // URL to serve the root page

				browser->navigate(use_url);

				this->loaded_video_url = video_URL;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error occured while created browser video player: " + e.what());
				gui_client->print("Error occured while created browser video player: " + e.what());
				this->state = State_ErrorOccurred;
			}
		}
	}
}


void BrowserVidPlayer::mouseReleased(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseReleased()");

	if(browser.nonNull())
		browser->mouseReleased(e, uv_coords);
}


void BrowserVidPlayer::mousePressed(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mousePressed(), uv_coords: " + uv_coords.toString());

	if(browser.nonNull())
		browser->mousePressed(e, uv_coords);
}


void BrowserVidPlayer::mouseDoubleClicked(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseDoubleClicked()");
}


void BrowserVidPlayer::mouseMoved(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseMoved(), uv_coords: " + uv_coords.toString());

	if(browser.nonNull())
		browser->mouseMoved(e, uv_coords);
}


void BrowserVidPlayer::wheelEvent(MouseWheelEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->wheelEvent(e, uv_coords);
}


void BrowserVidPlayer::keyPressed(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyPressed(e);
}


void BrowserVidPlayer::keyReleased(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyReleased(e);
}
