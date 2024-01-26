/*=====================================================================
WebViewData.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WebViewData.h"


#include "GUIClient.h"
#include "UIEvents.h"
#include "WorldState.h"
#include "EmbeddedBrowser.h"
#include "CEF.h"
#include "URLWhitelist.h"
#include "../shared/WorldObject.h"
#include "../audio/AudioEngine.h"
#include <qt/QtUtils.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <networking/URL.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/Base64.h>
#include <QtGui/QPainter>
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


static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, const std::string& text)
{
	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);

	QImage qimage(W, H, QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
	qimage.fill(QColor(220, 220, 220));
	QPainter painter(&qimage);
	painter.setPen(QPen(QColor(30, 30, 30)));
	painter.setFont(QFont("helvetica", 20, QFont::Normal));
	const int padding = 20;
	painter.drawText(QRect(padding, padding, W - padding*2, H - padding*2), Qt::TextWordWrap | Qt::AlignLeft, QtUtils::toQString(text));

	painter.drawRect(W/2 - 100, button_top_y, 200, button_H);

	//painter.setPen(QPen(QColor(30, 30, 30)));
	//painter.setFont(QFont("helvetica", 20, QFont::Normal));
	painter.drawText(QRect(button_left_x, button_top_y, /*width=*/button_W, /*height=*/button_H), Qt::AlignVCenter | Qt::AlignHCenter, "Load"); // y=0 at top

	OpenGLTextureRef tex = new OpenGLTexture(W, H, opengl_engine, ArrayRef<uint8>(NULL, 0), OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
	tex->loadIntoExistingTexture(/*mipmap level=*/0, W, H, /*row stride B=*/qimage.bytesPerLine(), ArrayRef<uint8>(qimage.constBits(), qimage.sizeInBytes()), /*bind_needed=*/true);
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

						assert(!showing_click_to_load_text);
						ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, "Click below to load " + ob->target_url); // TEMP
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
					ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, "Click below to load " + ob->target_url);
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

#if 0
	{
		if(time_since_last_webview_display.elapsed() > 0.0333) // Max 30 fps
		{
			if(webview_qimage.size() != webview->size())
			{
				webview_qimage = QImage(webview->size(), QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
			}

			const int W = webview_qimage.width();
			const int H = webview_qimage.height();

			QPainter painter(&webview_qimage);

			// Draw the web-view to the QImage
			webview->render(&painter);

			// Draw hovered-over link URL at bottom left
			if(!this->current_hovered_URL.isEmpty())
			{
				QFont system_font = QApplication::font();
				system_font.setPointSize(16);

				QFontMetrics metrics(system_font);
				QRect text_rect = metrics.boundingRect(this->current_hovered_URL);

				//printVar(text_rect.x());
				//printVar(text_rect.y());
				//printVar(text_rect.top());
				//printVar(text_rect.bottom());
				//printVar(text_rect.left());
				//printVar(text_rect.right());
				/*
				For example:
				text_rect.x(): 0
				text_rect.y(): -23
				text_rect.top(): -23
				text_rect.bottom(): 4
				text_rect.left(): 0
				text_rect.right(): 464
				*/
				const int x_padding = 12; // in pixels
				const int y_padding = 12; // in pixels
				const int link_W = text_rect.width()  + x_padding*2;
				const int link_H = -text_rect.top() + y_padding*2;

				painter.setPen(QPen(QColor(200, 200, 200)));
				painter.fillRect(0, H - link_H, link_W, link_H, QBrush(QColor(200, 200, 200), Qt::SolidPattern));

				painter.setPen(QPen(QColor(0, 0, 0)));
				painter.setFont(system_font);
				painter.drawText(QPoint(x_padding, /*font baseline y=*/H - y_padding), this->current_hovered_URL);
			}

			// Draw loading indicator
			if(loading_in_progress)
			{
				const int loading_bar_w = (int)((float)W * cur_load_progress / 100.f);
				const int loading_bar_h = 8;
				painter.fillRect(0, H - loading_bar_h, loading_bar_w, loading_bar_h, QBrush(QColor(100, 100, 255), Qt::SolidPattern));
			}

			painter.end();

			if(ob->opengl_engine_ob->materials[0].albedo_texture.isNull())
			{
				ob->opengl_engine_ob->materials[0].albedo_texture = new OpenGLTexture(W, H, opengl_engine, OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
			}

			// Update texture
			ob->opengl_engine_ob->materials[0].albedo_texture->load(W, H, /*row stride B=*/webview_qimage.bytesPerLine(), ArrayRef<uint8>(webview_qimage.constBits(), webview_qimage.sizeInBytes()));


			//webview_qimage.save("webview_qimage.png", "png");

			time_since_last_webview_display.reset();
		}
	}
#endif
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
