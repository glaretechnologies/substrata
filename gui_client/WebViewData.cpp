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
#if EMSCRIPTEN
#include <emscripten.h>
#endif


// Defined in BrowserVidPlayer.cpp
extern "C" void destroyHTMLViewJS(int handle);
extern "C" void setHTMLElementCSSTransform(int handle, const char* matrix_string);
extern "C" int makeHTMLViewJS();


WebViewData::WebViewData()
:	browser(NULL),
	showing_click_to_load_text(false),
	user_clicked_to_load(false),
	previous_is_visible(true),
	html_view_handle(-1),
	m_gui_client(NULL)
{}


WebViewData::~WebViewData()
{
	browser = NULL;

#if EMSCRIPTEN
	// If there is a browser, destroy it.
	if(html_view_handle >= 0)
	{
		destroyHTMLViewJS(html_view_handle);
		html_view_handle = -1;
	}
#endif
}


static const int text_tex_W = 512;
static const int button_W = 200;
static const int button_left_x = text_tex_W/2 - button_W/2;
static const int button_top_y = (int)((1080.0 / 1920) * text_tex_W) - 120;
static const int button_H = 60;


[[maybe_unused]] static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, GLUI* glui, const std::string& URL)
{
	TextRendererFontFace* font = glui->getFont(/*font size px=*/16, /*emoji=*/false);

	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);

	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);
	map->set(255);

	{
		const std::string text = "Click below to load";
		const TextRendererFontFace::SizeInfo size_info = font->getTextSize(text);
		font->drawText(*map, text, W/2 - size_info.glyphSize().x/2, H/2 - 23, Colour3f(0,0,0), /*render SDF=*/false);
	}
	{
		const TextRendererFontFace::SizeInfo size_info = font->getTextSize(URL);
		font->drawText(*map, URL, W/2 - size_info.glyphSize().x/2, H/2, Colour3f(0,0,0), /*render SDF=*/false);
	}

	const uint8 col[4] = { 30, 30, 30, 255 };
	Drawing::drawRect(*map, /*x0=*/W/2 - button_W/2, /*y0=*/button_top_y, /*wdith=*/button_W, /*height=*/button_H, col);


	const TextRendererFontFace::SizeInfo load_size_info = font->getTextSize("Load");
	font->drawText(*map, "Load", W/2 - load_size_info.glyphSize().x/2, button_top_y + button_H - 22, Colour3f(30.f / 255), /*render SDF=*/false);


	TextureParams params;
	params.use_mipmaps = false;
	params.filtering = OpenGLTexture::Filtering_Bilinear; // Disable trilinear filtering on the 'click to load' texture on webviews, to avoid stutters while drivers compute Mipmaps.
	params.allow_compression = false;
	OpenGLTextureRef tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("click_to_load_" + URL), *map, params);
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


#if EMSCRIPTEN


EM_JS(int, makeWebViewIframe, (const char* URL), {
	console.log("=================makeWebViewIframe()================");
	console.log("URL: " + UTF8ToString(URL));

	let URL_str = UTF8ToString(URL);

	// Get handle
	let handle = next_html_view_elem_handle;
	next_html_view_elem_handle++;

	let new_div = document.createElement('div');
	new_div.className = 'transformable-html-view';
	new_div.width = '1024';
	new_div.height = '576';
	new_div.id = 'transformable-html-view-' + handle.toString(); // Make unique ID


	// Make the iframe ourself so we can set credentialless attribute.
	let youtube_iframe = document.createElement('iframe');
	youtube_iframe.src = URL_str;
	youtube_iframe.width = '1024';
	youtube_iframe.height = '576';
	youtube_iframe.id = 'transformable-html-view-webview-' + handle.toString(); // Make unique ID
	youtube_iframe.setAttribute('credentialless', "");
	
	new_div.appendChild(youtube_iframe);

	document.getElementById('iframe-container-camera').appendChild(new_div);

	// insert into global handle->div map
	html_view_elem_handle_to_div_map[handle] = new_div;
	
	// console.log("makeWebViewIframe() done, returning handle " + handle.toString());
	return handle;
});


#endif // end if EMSCRIPTEN


void WebViewData::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

	m_gui_client = gui_client;

	const double ob_dist_from_cam = ob->pos.getDist(gui_client->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	[[maybe_unused]]const double unload_dist = maxBrowserDist() * 1.3;
	[[maybe_unused]] const bool in_process_dist = ob_dist_from_cam < max_play_dist;

#if EMSCRIPTEN
	if(in_process_dist)
	{
		if((html_view_handle < 0) && !ob->target_url.empty() && ob->opengl_engine_ob)
		{
			const bool URL_in_whitelist = gui_client->world_state->url_whitelist->isURLPrefixInWhitelist(ob->target_url);

			// If the user is logged in to their personal world, and the user created the object, consider the URL to be safe.
			const bool webview_is_safe = gui_client->logged_in_user_id.valid() && 
				(!gui_client->server_worldname.empty() && (gui_client->server_worldname == gui_client->logged_in_user_name)) && // If this is the personal world of the user:
				(gui_client->logged_in_user_id == ob->creator_id);

			if(user_clicked_to_load || URL_in_whitelist || webview_is_safe)
			{
				gui_client->logMessage("WebViewData: Making new web view iframe, target_url: " + ob->target_url);
				conPrint("WebViewData: Making new web view iframe, target_url: " + ob->target_url);

				html_view_handle = makeWebViewIframe(ob->target_url.c_str());

				conPrint("makeWebViewIframe() done, got handle " +  toString(html_view_handle));

				// Now that we are loading the iframe, draw this object with alpha zero.
				ob->opengl_engine_ob->materials[0].alpha = 0.f; // Set alpha to zero for alpha-cutout technique, to show web views in iframe under the opengl canvas.
				opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
			}
			else
			{
				if(ob->opengl_engine_ob->materials[0].emission_texture.isNull())
				{
					gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

					//assert(!showing_click_to_load_text);
					ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), ob->target_url);
					opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
					showing_click_to_load_text = true;
				}
			}

			// TODO: handle target_url changing

			this->loaded_target_url = ob->target_url;
		}
	}
	else // else if !in_process_dist:
	{
		if(ob_dist_from_cam >= unload_dist)
		{
			// If there is a browser, destroy it.
			if(html_view_handle >= 0)
			{
				destroyHTMLViewJS(html_view_handle);
				html_view_handle = -1;
			}
		}
	}


	// Set transform on the browser iframe div, if it exists.
	if(html_view_handle >= 0)
	{
		const float vw = (float)opengl_engine->getMainViewPortWidth();
		const float vh = (float)opengl_engine->getMainViewPortHeight();
		const float cam_dist = (opengl_engine->getCurrentScene()->lens_sensor_dist / opengl_engine->getCurrentScene()->use_sensor_width) * vw;

		const float elem_w_px = 1024;
		const float elem_h_px = 576;

		const Matrix4f transform = Matrix4f::translationMatrix(vw/2, vh/2 - elem_w_px, cam_dist) * 
			Matrix4f::scaleMatrix(1, -1, 1) * Matrix4f::translationMatrix(0, -elem_w_px, 0) * Matrix4f::uniformScaleMatrix(elem_w_px) * 
			opengl_engine->getCurrentScene()->last_view_matrix * ob->obToWorldMatrix() * 
			/*rot from x-y plane to x-z plane=*/Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) * Matrix4f::translationMatrix(0, 1, 0) * Matrix4f::scaleMatrix(1, -1, 1) * 
			Matrix4f::scaleMatrix(1 / elem_w_px, 1 / elem_h_px, 1 / elem_w_px);
			
		std::string matrix_string = "matrix3d(";
		for(int i=0; i<16; ++i)
		{
			matrix_string += ::toString(transform.e[i]);
			if(i + 1 < 16)
				matrix_string += ", ";
		}
		matrix_string += ")";
		setHTMLElementCSSTransform(html_view_handle, matrix_string.c_str());
	}
#endif // end if EMSCRIPTEN


#if CEF_SUPPORT

	const int viewport_width  = 1920;
	const int viewport_height = 1080; // Webview uses 1920 : 1080 aspect ratio.  (See MainWindow::on_actionAdd_Web_View_triggered())

	if(in_process_dist)
	{
		if(CEF::isInitialised())
		{
			if(browser.isNull() && !ob->target_url.empty() && ob->opengl_engine_ob.nonNull())
			{
				const bool URL_in_whitelist = gui_client->url_whitelist.isURLPrefixInWhitelist(ob->target_url);

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

						ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
					}

					browser = new EmbeddedBrowser();
					browser->create(ob->target_url, viewport_width, viewport_height, gui_client, ob, /*mat index=*/0, /*apply_to_emission_texture=*/true, opengl_engine);
				}
				else
				{
					if(ob->opengl_engine_ob->materials[0].emission_texture.isNull())
					{
						gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

						//assert(!showing_click_to_load_text);
						ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), ob->target_url);
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

				const bool URL_in_whitelist = gui_client->url_whitelist.isURLPrefixInWhitelist(ob->target_url);

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
					ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), ob->target_url);
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
			if(browser && ob->opengl_engine_ob)
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

	startInteractingWithWebViewEmscripten();
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

	startInteractingWithWebViewEmscripten();
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


void WebViewData::startInteractingWithWebViewEmscripten()
{
#if EMSCRIPTEN
	if(html_view_handle >= 0)
	{
		if(m_gui_client)
			m_gui_client->showInfoNotification("Interacting with web view");

		EM_ASM({
			document.getElementById('background-div').style.zIndex = 100; // move background to front

			move_background_timer_id = setTimeout(function() {
				document.getElementById('background-div').style.zIndex = -1; // move background to back
			}, 5000);
		});
	}
#endif
}
