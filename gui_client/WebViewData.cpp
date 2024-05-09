/*=====================================================================
WebViewData.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WebViewData.h"


#include "GUIClient.h"
#include "WorldState.h"
#include "EmbeddedBrowser.h"
#include "CEF.h"
#include "URLWhitelist.h"
#include "../shared/WorldObject.h"
#include "../audio/AudioEngine.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <networking/URL.h>
#include <graphics/Drawing.h>
#include <ui/UIEvents.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/Base64.h>
#include "superluminal/PerformanceAPI.h"


WebViewData::WebViewData()
:	browser(NULL),
	showing_click_to_load_text(false),
	user_clicked_to_load(false),
	previous_is_visible(true)
{}


WebViewData::~WebViewData()
{
	browser = NULL;
}


static const int text_tex_W = 512;
static const int button_W = 200;
static const int button_left_x = text_tex_W/2 - button_W/2;
static const int button_top_y = (int)((1080.0 / 1920) * text_tex_W) - 120;
static const int button_H = 60;


static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, GLUI* glui, const std::string& text)
{
	TextRendererFontFace* font = glui->getFont(/*font size px=*/16, /*emoji=*/false);

	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);

	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);
	map->set(255);

	const TextRendererFontFace::SizeInfo size_info = font->getTextSize(text);
	font->drawText(*map, text, W/2 - size_info.glyphSize().x/2, H/2, Colour3f(0,0,0), /*render SDF=*/false);

	const uint8 col[4] = { 30, 30, 30, 255 };
	Drawing::drawRect(*map, /*x0=*/W/2 - button_W/2, /*y0=*/button_top_y, /*wdith=*/button_W, /*height=*/button_H, col);


	const TextRendererFontFace::SizeInfo load_size_info = font->getTextSize("Load");
	font->drawText(*map, "Load", W/2 - load_size_info.glyphSize().x/2, button_top_y + button_H - 22, Colour3f(30.f / 255), /*render SDF=*/false);


	TextureParams params;
	params.use_mipmaps = false;
	params.allow_compression = false;
	OpenGLTextureRef tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey(text), *map, params);
	return tex;
}


static bool uvsAreOnLoadButton(float uv_x, float uv_y)
{
	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);
	const int x = (int)(uv_x * W);
	const int y = (int)((1.f - uv_y) * H);
	
	return
		x >= button_left_x && x <= (button_left_x + button_W) &&
		y >= button_top_y && y <= (button_top_y + button_H);
}


void WebViewData::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

#if CEF_SUPPORT

	const int width = 1024;
	const int height = (int)(1024.f / 1920.f * 1080.f); // Webview uses 1920 : 1080 aspect ratio.  (See MainWindow::on_actionAdd_Web_View_triggered())

	const double ob_dist_from_cam = ob->pos.getDist(gui_client->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	const bool in_process_dist = ob_dist_from_cam < max_play_dist;
	if(in_process_dist)
	{
		if(!CEF::isInitialised())
		{
			CEF::initialiseCEF(gui_client->base_dir_path);
		}

		if(CEF::isInitialised())
		{
			if(browser.isNull() && !ob->target_url.empty() && ob->opengl_engine_ob.nonNull())
			{
				const bool URL_in_whitelist = gui_client->world_state->url_whitelist->isURLPrefixInWhitelist(ob->target_url);

				// If the user is logged in to their personal world, and the user created the object, consider the URL to be safe.
				const bool webview_is_safe = gui_client->logged_in_user_id.valid() && 
					(!gui_client->server_worldname.empty() && (gui_client->server_worldname == gui_client->logged_in_user_name)) && // If this is the personal world of the user:
					(gui_client->logged_in_user_id == ob->creator_id);

				if(user_clicked_to_load || URL_in_whitelist || webview_is_safe)
				{
					gui_client->logMessage("Creating browser, target_url: " + ob->target_url);

					if(ob->opengl_engine_ob.nonNull())
					{
						gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

						std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
						ob->opengl_engine_ob->materials[0].emission_texture = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTexture::Format_SRGBA_Uint8,
							GL_SRGB8_ALPHA8, // GL internal format
							GL_BGRA, // GL format.
							OpenGLTexture::Filtering_Bilinear);

						//ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.

						opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
					}

					browser = new EmbeddedBrowser();
					browser->create(ob->target_url, ob->opengl_engine_ob->materials[0].emission_texture, gui_client, ob, opengl_engine);
				}
				else
				{
					if(ob->opengl_engine_ob->materials[0].emission_texture.isNull())
					{
						gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

						//assert(!showing_click_to_load_text);
						ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), "Click below to load " + ob->target_url);
						opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
						showing_click_to_load_text = true;
					}
				}

				this->loaded_target_url = ob->target_url;
			}

			// If target url has changed, tell webview to load it
			if(browser.nonNull() && (ob->target_url != this->loaded_target_url))
			{
				// conPrint("Webview loading URL '" + ob->target_url + "'...");

				const bool URL_in_whitelist = gui_client->world_state->url_whitelist->isURLPrefixInWhitelist(ob->target_url);

				// If the user is logged in to their personal world, and the user created the object, consider the URL to be safe.
				const bool webview_is_safe = gui_client->logged_in_user_id.valid() && 
					(!gui_client->server_worldname.empty() && (gui_client->server_worldname == gui_client->logged_in_user_name)) && // If this is the personal world of the user:
					(gui_client->logged_in_user_id == ob->creator_id);

				if(URL_in_whitelist || webview_is_safe)
				{
					browser->navigate(ob->target_url);

					this->loaded_target_url = ob->target_url;
				}
				else
				{
					gui_client->logMessage("Closing browser (URL changed), target_url: " + ob->target_url);
					browser = NULL;

					// Remove audio source
					if(ob->audio_source.nonNull())
					{
						gui_client->audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
					}

					gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

					user_clicked_to_load = false;
					ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), "Click below to load " + ob->target_url);
					opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
					showing_click_to_load_text = true;
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
	}
	else // else if !in_process_dist:
	{
		if(browser.nonNull())
		{
			gui_client->logMessage("Closing browser (out of view distance), target_url: " + ob->target_url);
			browser = NULL;

			// Remove audio source
			if(ob->audio_source.nonNull())
			{
				gui_client->audio_engine.removeSource(ob->audio_source);
				ob->audio_source = NULL;
			}
		}
	}
#endif // CEF_SUPPORT
}


void WebViewData::mouseReleased(MouseEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->mouseReleased(e, uv_coords);
}


void WebViewData::mousePressed(MouseEvent* e, const Vec2f& uv_coords)
{
	if(showing_click_to_load_text && uvsAreOnLoadButton(uv_coords.x, uv_coords.y))
		user_clicked_to_load = true;

	if(browser.nonNull())
		browser->mousePressed(e, uv_coords);
}


void WebViewData::mouseDoubleClicked(MouseEvent* e, const Vec2f& uv_coords)
{
}


void WebViewData::mouseMoved(MouseEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->mouseMoved(e, uv_coords);
}


void WebViewData::wheelEvent(MouseWheelEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->wheelEvent(e, uv_coords);
}


void WebViewData::keyPressed(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyPressed(e);
}


void WebViewData::keyReleased(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyReleased(e);
}


void WebViewData::handleTextInputEvent(TextInputEvent& e)
{
	if(browser.nonNull())
		browser->handleTextInputEvent(e);
}
