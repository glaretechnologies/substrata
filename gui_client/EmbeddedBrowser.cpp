/*=====================================================================
EmbeddedBrowser.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "EmbeddedBrowser.h"


#include "GUIClient.h"
#include "WorldState.h"
#include "CEFInternal.h"
#include "CEF.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "../audio/AudioEngine.h"
#include <ui/UIEvents.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <webserver/ResponseUtils.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/BufferInStream.h>
#include <utils/Base64.h>
#include "superluminal/PerformanceAPI.h"


#if CEF_SUPPORT  // CEF_SUPPORT will be defined in CMake (or not).
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>
#ifdef OSX
#include <wrapper/cef_library_loader.h>
#endif
#include <URL.h>
#endif


#if CEF_SUPPORT


// This class is shared among all browser instances, the browser the callback applies to is passed in as arg 0.
// The methods of this class will be called on the UI (main) thread.
class EmbeddedBrowserRenderHandler : public CefRenderHandler
{
public:
	EmbeddedBrowserRenderHandler(Reference<OpenGLTexture> opengl_tex_, GUIClient* gui_client_, WorldObject* ob_, OpenGLEngine* opengl_engine_)
	:	opengl_tex(opengl_tex_), opengl_engine(opengl_engine_), gui_client(gui_client_), ob(ob_), discarded_dirty_updates(false) /*discarded_dirty_rect(Vec2i(1000000,1000000), Vec2i(-1000000,-1000000))*/ {}

	~EmbeddedBrowserRenderHandler() {}

	void onWebViewDataDestroyed()
	{
		CEF_REQUIRE_UI_THREAD();

		opengl_tex = NULL;
		opengl_engine = NULL;
		ob = NULL;
	}

	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(opengl_tex.nonNull())
			rect = CefRect(0, 0, (int)opengl_tex->xRes(), (int)opengl_tex->yRes());
		else
			rect = CefRect(0, 0, 100, 100);
	}

	// "|buffer| will be |width|*|height|*4 bytes in size and represents a BGRA image with an upper-left origin"
	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirty_rects, const void* buffer, int width, int height) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(opengl_engine && ob)
		{
			//conPrint("OnPaint()");

			// whole page was updated
			if(type == PET_VIEW)
			{
				const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
				if(ob_visible)
				{
					for(size_t i=0; i<dirty_rects.size(); ++i)
					{
						const CefRect& rect = dirty_rects[i];

						//conPrint("Updating dirty rect " + toString(rect.x) + ", " + toString(rect.y) + ", w: " + toString(rect.width) + ", h: " + toString(rect.height));

						// Copy dirty rect data into a packed buffer

						gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while uploading to texture buffer.

						const uint8* start_px = (uint8*)buffer + (width * 4) * rect.y + 4 * rect.x;
						opengl_tex->loadRegionIntoExistingTexture(/*mip level=*/0, rect.x, rect.y, rect.width, rect.height, /*row stride (B) = */width * 4, ArrayRef<uint8>(start_px, width * rect.height * 4), /*bind_needed=*/true);
					}
				}
				else
				{
					//conPrint("Discarded a dirty rect update");
					discarded_dirty_updates = true;

					//for(size_t i=0; i<dirty_rects.size(); ++i)
					//{
					//	const CefRect& rect = dirty_rects[i];
					//	
					//	//discarded_dirty_rect.enlargeToHoldRect(Rect2i(Vec2i(rect.x, rect.y), Vec2i(rect.x + rect.width, rect.y + rect.height)));
					//}
				}


				// if there is still a popup open, write it into the page too (it's pixels will have been
				// copied into it's buffer by a call to OnPaint with type of PET_POPUP earlier)
				//if(gPopupPixels != nullptr)
				//{
				//    unsigned char* dst = gPagePixels + gPopupRect.y * gWidth * gDepth + gPopupRect.x * gDepth;
				//    unsigned char* src = (unsigned char*)gPopupPixels;
				//    while(src < (unsigned char*)gPopupPixels + gPopupRect.width * gPopupRect.height * gDepth)
				//    {
				//        memcpy(dst, src, gPopupRect.width * gDepth);
				//        src += gPopupRect.width * gDepth;
				//        dst += gWidth * gDepth;
				//    }
				//}

			}
			// popup was updated
			else if(type == PET_POPUP)
			{
				//std::cout << "OnPaint() for popup: " << width << " x " << height << " at " << gPopupRect.x << " x " << gPopupRect.y << std::endl;

				// copy over the popup pixels into it's buffer
				// (popup buffer created in onPopupSize() as we know the size there)
				//memcpy(gPopupPixels, buffer, width * height * gDepth);
				//
				//// copy over popup pixels into page pixels. We need this for when popup is changing (e.g. highlighting or scrolling)
				//// when the containing page is not changing and therefore doesn't get an OnPaint update
				//unsigned char* src = (unsigned char*)gPopupPixels;
				//unsigned char* dst = gPagePixels + gPopupRect.y * gWidth * gDepth + gPopupRect.x * gDepth;
				//while(src < (unsigned char*)gPopupPixels + gPopupRect.width * gPopupRect.height * gDepth)
				//{
				//    memcpy(dst, src, gPopupRect.width * gDepth);
				//    src += gPopupRect.width * gDepth;
				//    dst += gWidth * gDepth;
				//}
			}
		}
	}


	// Called to retrieve the translation from view coordinates to actual screen
	// coordinates. Return true if the screen coordinates were provided.
	virtual bool GetScreenPoint(CefRefPtr<CefBrowser> browser, int viewX, int viewY, int& screenX, int& screenY) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(!opengl_engine || opengl_tex.isNull())
			return false;

		//conPrint("GetScreenPoint: viewX: " + toString(viewX) + ", viewY: " + toString(viewY));
		
		const Matrix4f ob_to_world = obToWorldMatrix(*this->ob);

		// Work out if the camera is viewing the front face or the back face of the webview object.
		const Vec4f campos_ws = opengl_engine->getCameraPositionWS();
		const Vec4f obpos_ws = ob_to_world * Vec4f(0, 0, 0, 1);
		const Vec4f ob_to_cam_ws = campos_ws - obpos_ws;

		const Vec4f frontface_vec_os = Vec4f(0, -1, 0, 0); // Vector pointing out of the webview front face
		const Vec4f frontface_vec_ws = ob_to_world * frontface_vec_os;

		const bool viewing_frontface = dot(frontface_vec_ws, ob_to_cam_ws) > 0;

		float u = viewX / (float)opengl_tex->xRes();
		if(!viewing_frontface) // Flip u coordinate if we are viewing the backface.
			u = 1 - u;
		const float v = 1.f - viewY / (float)opengl_tex->yRes();

		const Vec4f pos_os(u, 0, v, 1);

		const Vec4f pos_ws = ob_to_world * pos_os;

		Vec2f window_coords;
		opengl_engine->getWindowCoordsForWSPos(pos_ws, window_coords);

		const Vec2i gl_widget_pos = gui_client->getGlWidgetPosInGlobalSpace();

		screenX = (int)(window_coords.x + gl_widget_pos.x);
		screenY = (int)(window_coords.y + gl_widget_pos.y);

		return true;
	}

	void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override
	{
		CEF_REQUIRE_UI_THREAD();
		//std::cout << "CefRenderHandler::OnPopupShow(" << (show ? "true" : "false") << ")" << std::endl;

		if(!show)
		{
			//delete gPopupPixels;
			//gPopupPixels = nullptr;
			//gPopupRect.Reset();
		}
	}

	void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override
	{
		CEF_REQUIRE_UI_THREAD();
		//std::cout << "CefRenderHandler::OnPopupSize(" << rect.width << " x " << rect.height << ") at " << rect.x << ", " << rect.y << std::endl;

		//gPopupRect = rect;

		/*if(gPopupPixels == nullptr)
		{
			gPopupPixels = new unsigned char[rect.width * rect.height * gDepth];
		}*/
	}

	Reference<OpenGLTexture> opengl_tex;
	OpenGLEngine* opengl_engine;
	GUIClient* gui_client;
	WorldObject* ob;
	bool discarded_dirty_updates; // Set to true if we didn't update the buffer when a rectangle was dirty, because the webview object was not visible by the camera.
	//Rect2i discarded_dirty_rect;


	IMPLEMENT_REFCOUNTING(EmbeddedBrowserRenderHandler);
};


// Reads a resource off disk and returns the data to CEF via the URL https://resource/xx
// Also returns root_page from https://localdomain/.
class EmbeddedBrowserResourceHandler : public CefResourceHandler
{
public:
	EmbeddedBrowserResourceHandler(Reference<ResourceManager> resource_manager_, const std::string& root_page_) : in_stream(NULL), resource_manager(resource_manager_), root_page(root_page_) {}
	~EmbeddedBrowserResourceHandler() { delete in_stream; }

	// Open the response stream. To handle the request immediately set
	// |handle_request| to true and return true. To decide at a later time set
	// |handle_request| to false, return true, and execute |callback| to continue
	// or cancel the request. To cancel the request immediately set
	// |handle_request| to true and return false. This method will be called in
	// sequence but not from a dedicated thread. For backwards compatibility set
	// |handle_request| to false and return false and the ProcessRequest method
	// will be called.
	virtual bool Open(CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback> callback) override
	{
		handle_request = false; // May be overwritten below

		const std::string URL = request->GetURL().ToString();

		//conPrint("\n" + doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 2) + ":------------------AnimatedTexResourceHandler::Open(): URL: " + URL + "------------------");

		if(hasPrefix(URL, "https://localdomain/"))
		{
			handle_request = true;

			in_stream = new BufferInStream(root_page);

			response_mime_type = "text/html";
			return true;
		}

		if(hasPrefix(URL, "https://resource/"))
		{
			const std::string resource_URL = eatPrefix(URL, "https://resource/");
			//conPrint("resource_URL: " + resource_URL);

			ResourceRef resource = resource_manager->getExistingResourceForURL(resource_URL);
			if(resource.nonNull())
			{
				const std::string path = resource_manager->getLocalAbsPathForResource(*resource);
				//conPrint("path: " + path);
				try
				{
					in_stream = new FileInStream(path);

					response_mime_type = web::ResponseUtils::getContentTypeForPath(resource_URL);

					handle_request = true;
					return true;
				}
				catch(glare::Exception&)
				{
					return false;
				}
			}
			else
			{
				// No such resource present.
				return false;
			}
		}

		return false;
	}


	// Retrieve response header information. If the response length is not known
	// set |response_length| to -1 and ReadResponse() will be called until it
	// returns false. If the response length is known set |response_length|
	// to a positive value and ReadResponse() will be called until it returns
	// false or the specified number of bytes have been read. Use the |response|
	// object to set the mime type, http status code and other optional header
	// values. To redirect the request to a new URL set |redirectUrl| to the new
	// URL. |redirectUrl| can be either a relative or fully qualified URL.
	// It is also possible to set |response| to a redirect http status code
	// and pass the new URL via a Location header. Likewise with |redirectUrl| it
	// is valid to set a relative or fully qualified URL as the Location header
	// value. If an error occured while setting up the request you can call
	// SetError() on |response| to indicate the error condition.
	virtual void GetResponseHeaders(CefRefPtr<CefResponse> response, int64& response_length, CefString& redirectUrl) override
	{
		response->SetMimeType(response_mime_type);

		if(in_stream)
			response_length = (int64)this->in_stream->size();
		else
			response_length = -1;
	}


	// Skip response data when requested by a Range header. Skip over and discard
	// |bytes_to_skip| bytes of response data. If data is available immediately
	// set |bytes_skipped| to the number of bytes skipped and return true. To
	// read the data at a later time set |bytes_skipped| to 0, return true and
	// execute |callback| when the data is available. To indicate failure set
	// |bytes_skipped| to < 0 (e.g. -2 for ERR_FAILED) and return false. This
	// method will be called in sequence but not from a dedicated thread.
	virtual bool Skip(int64 bytes_to_skip,
		int64& bytes_skipped,
		CefRefPtr<CefResourceSkipCallback> callback) override
	{
		//conPrint("Skipping " + toString(bytes_to_skip) + " B...");
		if(in_stream)
		{
			try
			{
				// There seems to be a CEF bug where are a Skip call is made at the start of a resource read, after a video repeats.
				// This breaks video looping.  Work around it by detecting the skip call and doing nothing in that case.
				// See https://magpcss.org/ceforum/viewtopic.php?f=6&t=19171
				if(in_stream->getReadIndex() != 0)
					in_stream->advanceReadIndex(bytes_to_skip);

				bytes_skipped = bytes_to_skip;
				return true;
			}
			catch(glare::Exception&)
			{
				bytes_skipped = -2;
				return false;
			}
		}
		else
		{
			bytes_skipped = -2;
			return false;
		}
	}


	// Read response data. If data is available immediately copy up to
	// |bytes_to_read| bytes into |data_out|, set |bytes_read| to the number of
	// bytes copied, and return true. To read the data at a later time keep a
	// pointer to |data_out|, set |bytes_read| to 0, return true and execute
	// |callback| when the data is available (|data_out| will remain valid until
	// the callback is executed). To indicate response completion set |bytes_read|
	// to 0 and return false. To indicate failure set |bytes_read| to < 0 (e.g. -2
	// for ERR_FAILED) and return false. This method will be called in sequence
	// but not from a dedicated thread. For backwards compatibility set
	// |bytes_read| to -1 and return false and the ReadResponse method will be
	// called.
	virtual bool Read(void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefResourceReadCallback> callback) override
	{
		if(in_stream)
		{
			try
			{
				//conPrint("Reading up to " + toString(bytes_to_read) + " B, getReadIndex: " + toString(file_in_stream->getReadIndex()));
				const size_t bytes_available = in_stream->size() - in_stream->getReadIndex(); // bytes still available to read in the file
				if(bytes_available == 0)
				{
					bytes_read = 0; // indicate response completion (EOF)
					return false;
				}
				else
				{
					const size_t use_bytes_to_read = myMin((size_t)bytes_to_read, bytes_available);
					in_stream->readData(data_out, use_bytes_to_read);
					bytes_read = (int)use_bytes_to_read;
					return true;
				}
			}
			catch(glare::Exception&)
			{
				bytes_read = -2;
				return false;
			}
		}
		else
		{
			bytes_read = -2;
			return false;
		}
	}


	// Request processing has been canceled.
	virtual void Cancel() override
	{
		// conPrint("\n" + doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 2) + "------------------AnimatedTexResourceHandler::Cancel()------------------");
	}

	std::string root_page;
	std::string response_mime_type;
	RandomAccessInStream* in_stream;
	Reference<ResourceManager> resource_manager;

	IMPLEMENT_REFCOUNTING(EmbeddedBrowserResourceHandler);
};


class EmbeddedBrowserCefClient : public CefClient, public CefRequestHandler, public CefLoadHandler, public CefDisplayHandler, public CefAudioHandler, public CefCommandHandler, public CefContextMenuHandler, public CefResourceRequestHandler
{
public:
	EmbeddedBrowserCefClient(GUIClient* gui_client_, WorldObject* ob_) : num_channels(0), sample_rate(0), m_gui_client(gui_client_), m_ob(ob_) {}

	// Remove references to gui_client and ob as they may be destroyed while this EmbeddedBrowserCefClient object is still alive.
	void onWebViewDataDestroyed()
	{
		CEF_REQUIRE_UI_THREAD();

		{
			Lock lock(mutex);
			m_gui_client = NULL;
			m_ob = NULL;
		}
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return mRenderHandler; }

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return mLifeSpanHandler; }

	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

	CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }

	CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }

	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }

	// From CefDisplayHandler
	virtual bool OnConsoleMessage( CefRefPtr< CefBrowser > browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line ) override
	{
		conPrint("OnConsoleMessage: " + message.ToString());
		return true;
	}


	
	//-----------------------CefRequestHandler-----------------------------

	bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		const CefString& target_url,
		CefRequestHandler::WindowOpenDisposition target_disposition,
		bool user_gesture) override
	{
		return true;
	}

	// Called on the browser process IO thread before a resource request is
	// initiated. The |browser| and |frame| values represent the source of the
	// request. |request| represents the request contents and cannot be modified
	// in this callback. |is_navigation| will be true if the resource request is a
	// navigation. |is_download| will be true if the resource request is a
	// download. |request_initiator| is the origin (scheme + domain) of the page
	// that initiated the request. Set |disable_default_handling| to true to
	// disable default handling of the request, in which case it will need to be
	// handled via CefResourceRequestHandler::GetResourceHandler or it will be
	// canceled. To allow the resource load to proceed with default handling
	// return NULL. To specify a handler for the resource return a
	// CefResourceRequestHandler object. If this callback returns NULL the same
	// method will be called on the associated CefRequestContextHandler, if any.
	virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool is_navigation,
		bool is_download,
		const CefString& request_initiator,
		bool& disable_default_handling) override
	{
		// conPrint("Request URL: " + request->GetURL().ToString());

		const std::string URL = request->GetURL().ToString();

		if(hasPrefix(URL, "https://resource/") ||
			hasPrefix(URL, "https://localdomain/"))
		{
			// conPrint("interecepting resource request");
			disable_default_handling = true;
			return this;
		}
		return nullptr;
	}

	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool user_gesture,
		bool is_redirect) override

	{
		/*std::string frame_name = frame->GetName();
		std::string frame_url = frame->GetURL();

		conPrint("OnBeforeBrowse  is called: ");
		conPrint("                  Name is: " + std::string(frame_name));
		conPrint("                   URL is: " + std::string(frame_url));*/

		return false;
	}


	//---------------------------- CefResourceRequestHandler ----------------------------
	// Called on the IO thread before a resource is loaded. The |browser| and
	// |frame| values represent the source of the request, and may be NULL for
	// requests originating from service workers or CefURLRequest. To allow the
	// resource to load using the default network loader return NULL. To specify a
	// handler for the resource return a CefResourceHandler object. The |request|
	// object cannot not be modified in this callback.
	virtual CefRefPtr<CefResourceHandler> GetResourceHandler(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request) override
	{
		//conPrint("GetResourceHandler() Request URL: " + request->GetURL().ToString());

		// TODO: check URL prefix for https://resource/, return new AnimatedTexResourceHandler only in that case?
		// Will we need to handle other URLS here?

		Reference<ResourceManager> resource_manager;
		{
			Lock lock(mutex);
			resource_manager = this->m_gui_client->resource_manager;
		}

		return new EmbeddedBrowserResourceHandler(resource_manager, root_page);
		//return nullptr;
	}

	//---------------------------- CefLoadHandler ----------------------------
	void OnLoadStart(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		TransitionType transition_type) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(frame->IsMain())
		{
			//conPrint("Loading started");
		}
	}

	void OnLoadEnd(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		int httpStatusCode) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(frame->IsMain())
		{
			const std::string url = frame->GetURL();

			//conPrint("Load ended for URL: " + std::string(url) + " with HTTP status code: " + toString(httpStatusCode));
		}
	}

	//---------------------------- CefDisplayHandler ----------------------------
	// The methods of this class will be called on the UI thread.

	virtual bool OnCursorChange(CefRefPtr<CefBrowser> browser,
		CefCursorHandle cursor,
		cef_cursor_type_t type,
		const CefCursorInfo& custom_cursor_info) override
	{
		return false;
	}

	virtual void OnStatusMessage(CefRefPtr<CefBrowser> browser,
		const CefString& value) override
	{
		CEF_REQUIRE_UI_THREAD();

		GUIClient* gui_client;
		{
			Lock lock(mutex);
			gui_client = this->m_gui_client;
		}

		if(value.c_str())
		{
			//conPrint("OnStatusMessage: " + StringUtils::PlatformToUTF8UnicodeEncoding(value.c_str()));

			if(gui_client)
				gui_client->webViewDataLinkHovered(value.ToString());
			//if(web_view_data)
			//	web_view_data->linkHoveredSignal(QtUtils::toQString(value.ToString()));
		}
		else
		{
			//conPrint("OnStatusMessage: NULL");

			//if(web_view_data)
			//	web_view_data->linkHoveredSignal("");
			if(gui_client)
				gui_client->webViewDataLinkHovered("");
		}
	}

	//--------------------- CefCommandHandler ----------------------------
	// Called to execute a Chrome command triggered via menu selection or keyboard
	// shortcut. Values for |command_id| can be found in the cef_command_ids.h
	// file. |disposition| provides information about the intended command target.
	// Return true if the command was handled or false for the default
	// implementation. For context menu commands this will be called after
	// CefContextMenuHandler::OnContextMenuCommand. Only used with the Chrome
	// runtime.
	virtual bool OnChromeCommand(CefRefPtr<CefBrowser> browser, int command_id, cef_window_open_disposition_t disposition) override
	{
		return false;
	}


	//--------------------- CefContextMenuHandler ----------------------------
	// Called before a context menu is displayed. |params| provides information
	// about the context menu state. |model| initially contains the default
	// context menu. The |model| can be cleared to show no context menu or
	// modified to show a custom menu. Do not keep references to |params| or
	// |model| outside of this callback.
	virtual void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) override
	{
		// Context menus are crashing CEF on mac currently, see https://magpcss.org/ceforum/viewtopic.php?f=10&t=17959
		// This is probably fixable somehow, but for now just don't pop up context menus on mac.
#ifdef OSX
		model->Clear();
#endif
	}


	//--------------------- AudioHandler ----------------------------
	// Called on the UI thread to allow configuration of audio stream parameters.
	// Return true to proceed with audio stream capture, or false to cancel it.
	// All members of |params| can optionally be configured here, but they are
	// also pre-filled with some sensible defaults.
	virtual bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
		CefAudioParameters& params) override 
	{
		CEF_REQUIRE_UI_THREAD();

		GUIClient* gui_client;
		WorldObject* ob;
		{
			Lock lock(mutex);
			gui_client = this->m_gui_client;
			ob = this->m_ob;
		}

		if(!gui_client)
			return false;

		params.sample_rate = gui_client->audio_engine.getSampleRate();

		// Create audio source.  Do this now while we're in the main (UI) thread.
		if(ob && ob->audio_source.isNull())
		{
			// conPrint("OnAudioStreamStarted(), creating audio src");

			// Create a streaming audio source.
			this->audio_source = new glare::AudioSource(); // Hang on to a reference we can use from the audio stream thread.
			audio_source->type = glare::AudioSource::SourceType_Streaming;
			audio_source->pos = ob->getCentroidWS();
			audio_source->debugname = "webview: " + ob->target_url;
			audio_source->sampling_rate = gui_client->audio_engine.getSampleRate();
			audio_source->volume = myClamp(ob->audio_volume, 0.f, 10.f);

			{
				Lock lock(gui_client->world_state->mutex);

				const Parcel* parcel = gui_client->world_state->getParcelPointIsIn(ob->pos);
				audio_source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.
			}

			ob->audio_source = audio_source;
			gui_client->audio_engine.addSource(audio_source);
		}

		return true;
	}

	// Called on a browser audio capture thread when the browser starts
	// streaming audio. OnAudioStreamStopped will always be called after
	// OnAudioStreamStarted; both methods may be called multiple times
	// for the same browser. |params| contains the audio parameters like
	// sample rate and channel layout. |channels| is the number of channels.
	virtual void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
		const CefAudioParameters& params,
		int channels) override
	{
		sample_rate = params.sample_rate;
		num_channels = channels;
	}

	// Called on the audio stream thread when a PCM packet is received for the
	// stream. |data| is an array representing the raw PCM data as a floating
	// point type, i.e. 4-byte value(s). |frames| is the number of frames in the
	// PCM packet. |pts| is the presentation timestamp (in milliseconds since the
	// Unix Epoch) and represents the time at which the decompressed packet should
	// be presented to the user. Based on |frames| and the |channel_layout| value
	// passed to OnAudioStreamStarted you can calculate the size of the |data|
	// array in bytes.
	virtual void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
		const float** data,
		int frames,
		int64 pts) override
	{
		// NOTE: this method is not called on the main thread!
		// So we don't want to access ob or ob->audio_source as it may be being destroyed or modified on the main thread etc.

		// Copy to our audio source
		if(this->audio_source.nonNull())
		{
			const int num_samples = frames;
			temp_buf.resize(num_samples);

			if(num_channels == 1)
			{
				const float* const data0 = data[0];
				for(int z=0; z<num_samples; ++z)
					temp_buf[z] =  data0[z];
			}
			else if(num_channels == 2)
			{
				const float* const data0 = data[0];
				const float* const data1 = data[1];

				for(int z=0; z<num_samples; ++z)
				{
					const float left =  data0[z];
					const float right = data1[z];
					temp_buf[z] = (left + right) * 0.5f;
				}
			}
			else
			{
				for(int z=0; z<num_samples; ++z)
					temp_buf[z] = 0.f;
			}

			// We are accessing gui_client, that might have been set to null in another thread via onWebViewDataDestroyed(), so lock mutex.
			// We need to hold this mutex the entire time we using m_gui_client, to prevent gui_client closing in another thread.
			{
				Lock lock(mutex);
				if(m_gui_client)
				{
					Lock audio_engine_lock(m_gui_client->audio_engine.mutex);
					this->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
				}
			}
		}
	}

	// Called on the UI thread when the stream has stopped. OnAudioSteamStopped
	// will always be called after OnAudioStreamStarted; both methods may be
	// called multiple times for the same stream.
	virtual void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override
	{
		/*if(ob && ob->audio_source.nonNull())
		{
			gui_client->audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
		}*/
	}

	// Called on the UI or audio stream thread when an error occurred. During the
	// stream creation phase this callback will be called on the UI thread while
	// in the capturing phase it will be called on the audio stream thread. The
	// stream will be stopped immediately.
	virtual void OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString& message) override
	{
		conPrint("=========================================\nOnAudioStreamError: " + message.ToString());
	}
	// ----------------------------------------------------------------------------


	CefRefPtr<EmbeddedBrowserRenderHandler> mRenderHandler;
	CefRefPtr<CefLifeSpanHandler> mLifeSpanHandler; // The lifespan handler has references to the CefBrowsers, so the browsers should 

	Mutex mutex; // Protects gui_client, ob
	GUIClient* m_gui_client		GUARDED_BY(mutex);
	WorldObject* m_ob			GUARDED_BY(mutex);

	Reference<glare::AudioSource> audio_source; // Store a direct reference to the audio_source, to make sure it's alive while we are using it.
	int num_channels;
	int sample_rate;

	std::vector<float> temp_buf;

	std::string root_page;

	IMPLEMENT_REFCOUNTING(EmbeddedBrowserCefClient);
};


class EmbeddedBrowserCEFBrowser : public RefCounted
{
public:
	EmbeddedBrowserCEFBrowser(EmbeddedBrowserRenderHandler* render_handler, LifeSpanHandler* lifespan_handler, GUIClient* gui_client_, WorldObject* ob_, const std::string& root_page)
	:	mRenderHandler(render_handler),
		gui_client(gui_client_),
		ob(ob_)
	{
		cef_client = new EmbeddedBrowserCefClient(gui_client, ob);
		cef_client->root_page = root_page;
		cef_client->mRenderHandler = mRenderHandler;
		cef_client->mLifeSpanHandler = lifespan_handler;
	}

	virtual ~EmbeddedBrowserCEFBrowser()
	{
	}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
	void onWebViewDataDestroyed()
	{
		//conPrint("WebViewCEFBrowser::onWebViewDataDestroyed()");

		cef_client->onWebViewDataDestroyed();
		mRenderHandler->onWebViewDataDestroyed();
		//web_view_data = NULL;
	}


	void sendMouseClickEvent(CefBrowserHost::MouseButtonType btn_type, float uv_x, float uv_y, bool mouse_up, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost() && mRenderHandler->opengl_tex.nonNull())
		{
			//mBrowser->GetHost()->SendFocusEvent(true);

			CefMouseEvent cef_mouse_event;
			//cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			int last_click_count = 1;
			cef_browser->GetHost()->SendMouseClickEvent(cef_mouse_event, btn_type, mouse_up, last_click_count);
		}
	}

	void sendMouseMoveEvent(float uv_x, float uv_y, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost() && mRenderHandler->opengl_tex.nonNull())
		{
			CefMouseEvent cef_mouse_event;
			//cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			bool mouse_leave = false;
			cef_browser->GetHost()->SendMouseMoveEvent(cef_mouse_event, mouse_leave);
		}
	}

	void sendMouseWheelEvent(float uv_x, float uv_y, int delta_x, int delta_y, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost() && mRenderHandler->opengl_tex.nonNull())
		{
			CefMouseEvent cef_mouse_event;
			//cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			cef_browser->GetHost()->SendMouseWheelEvent(cef_mouse_event, (int)delta_x, (int)delta_y);
		}
	}

	void sendKeyEvent(cef_key_event_type_t event_type, int key, int native_virtual_key, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			//mBrowser->GetHost()->SetFocus(true);

			CefKeyEvent key_event;
			//key_event.Reset();
			key_event.type = event_type;
			//key_event.character = (char16)key;
			//key_event.unmodified_character = (char16)key;

			if(event_type == KEYEVENT_CHAR) // key >= 'a' && key <= 'z')
			{
				key_event.character = (char16_t)key;
				key_event.unmodified_character = (char16_t)key;

				key_event.windows_key_code = (char16_t)key; // TEMP native_virtual_key; // ??
				key_event.native_key_code = (char16_t)key; // TEMP native_virtual_key; // ??
			}
			else
			{
				key_event.windows_key_code = native_virtual_key; // ??
				key_event.native_key_code = native_virtual_key; // ??
			}
			key_event.modifiers = cef_modifiers;
			//key_event.focus_on_editable_field = true;//TEMP
			cef_browser->GetHost()->SendKeyEvent(key_event);
		}
	}

	void sendBackMousePress()
	{
		if(cef_browser)
			cef_browser->GoBack();
	}

	void sendForwardsMousePress()
	{
		if(cef_browser)
			cef_browser->GoForward();
	}

	void navigate(const std::string& url)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			cef_browser->GetMainFrame()->LoadURL(url);
		}
	}

	void requestExit()
	{
		if(cef_browser.get() && cef_browser->GetHost())
		{
			cef_browser->GetHost()->CloseBrowser(/*force_close=*/true);
		}
	}

	GUIClient* gui_client;
	WorldObject* ob;

	CefRefPtr<EmbeddedBrowserRenderHandler> mRenderHandler;
	CefRefPtr<CefBrowser> cef_browser;
	CefRefPtr<EmbeddedBrowserCefClient> cef_client;
};


static Reference<EmbeddedBrowserCEFBrowser> createBrowser(const std::string& URL, Reference<OpenGLTexture> opengl_tex, GUIClient* gui_client, WorldObject* ob, OpenGLEngine* opengl_engine,
	const std::string& root_page)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

	Reference<EmbeddedBrowserCEFBrowser> browser = new EmbeddedBrowserCEFBrowser(new EmbeddedBrowserRenderHandler(opengl_tex, gui_client, ob, opengl_engine), CEF::getLifespanHandler(), gui_client, ob,
		root_page);

	CefWindowInfo window_info;
	window_info.windowless_rendering_enabled = true;

	CefBrowserSettings browser_settings;
	browser_settings.windowless_frame_rate = 60;
	browser_settings.background_color = CefColorSetARGB(255, 100, 100, 100);

	browser->cef_browser = CefBrowserHost::CreateBrowserSync(window_info, browser->cef_client, CefString(URL), browser_settings, nullptr, nullptr);
	if(!browser->cef_browser)
		throw glare::Exception("Failed to create CEF browser");

	// There is a brief period before the audio capture kicks in, resulting in a burst of loud sound.  We can work around this by setting to mute here.
	// See https://bitbucket.org/chromiumembedded/cef/issues/3319/burst-of-uncaptured-audio-after-creating
	browser->cef_browser->GetHost()->SetAudioMuted(true);
	return browser;
}


#else // else if !CEF_SUPPORT: 

class EmbeddedBrowserCEFBrowser : public RefCounted
{};

#endif // CEF_SUPPORT


EmbeddedBrowser::EmbeddedBrowser()
:	embedded_cef_browser(NULL)
	//showing_click_to_load_text(false),
	//user_clicked_to_load(false)
{

}


EmbeddedBrowser::~EmbeddedBrowser()
{
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
	{
		embedded_cef_browser->onWebViewDataDestroyed(); // The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
		embedded_cef_browser->requestExit();
		embedded_cef_browser = NULL;
	}
#endif
}


void EmbeddedBrowser::create(const std::string& URL, Reference<OpenGLTexture> opengl_tex, GUIClient* gui_client, WorldObject* ob, OpenGLEngine* opengl_engine, const std::string& root_page)
{
#if CEF_SUPPORT
	this->embedded_cef_browser = NULL;
	this->embedded_cef_browser = createBrowser(URL, opengl_tex, gui_client, ob, opengl_engine, root_page);
#endif
}


void EmbeddedBrowser::updateRootPage(const std::string& root_page)
{
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->cef_client->root_page = root_page;
#endif
}


void EmbeddedBrowser::navigate(const std::string& URL)
{
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->navigate(URL);
#endif
}


void EmbeddedBrowser::browserBecameVisible()
{
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
	{
		if(embedded_cef_browser->mRenderHandler->discarded_dirty_updates)
		{
			// conPrint("Browser had disacarded dirty updates, invalidating...");
			embedded_cef_browser->mRenderHandler->discarded_dirty_updates = false;

			if(embedded_cef_browser->cef_browser && embedded_cef_browser->cef_browser->GetHost())
				embedded_cef_browser->cef_browser->GetHost()->Invalidate(PET_VIEW);
		}
	}
#endif
}


//static const int text_tex_W = 512;
//static const int button_W = 200;
//static const int button_left_x = text_tex_W/2 - button_W/2;
//static const int button_top_y = (int)((1080.0 / 1920) * text_tex_W) - 120;
//static const int button_H = 60;


//static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, const std::string& text)
//{
//	const int W = text_tex_W;
//	const int H = (int)((1080.0 / 1920) * W);
//
//	QImage qimage(W, H, QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
//	qimage.fill(QColor(220, 220, 220));
//	QPainter painter(&qimage);
//	painter.setPen(QPen(QColor(30, 30, 30)));
//	painter.setFont(QFont("helvetica", 20, QFont::Normal));
//	const int padding = 20;
//	painter.drawText(QRect(padding, padding, W - padding*2, H - padding*2), Qt::TextWordWrap | Qt::AlignLeft, QtUtils::toQString(text));
//
//	painter.drawRect(W/2 - 100, button_top_y, 200, button_H);
//
//	//painter.setPen(QPen(QColor(30, 30, 30)));
//	//painter.setFont(QFont("helvetica", 20, QFont::Normal));
//	painter.drawText(QRect(button_left_x, button_top_y, /*width=*/button_W, /*height=*/button_H), Qt::AlignVCenter | Qt::AlignHCenter, "Load"); // y=0 at top
//
//	OpenGLTextureRef tex = new OpenGLTexture(W, H, opengl_engine, ArrayRef<uint8>(NULL, 0), OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
//	tex->loadIntoExistingTexture(/*mipmap level=*/0, W, H, /*row stride B=*/qimage.bytesPerLine(), ArrayRef<uint8>(qimage.constBits(), qimage.sizeInBytes()), /*bind_needed=*/true);
//	return tex;
//}
//
//
//static bool uvsAreOnLoadButton(float uv_x, float uv_y)
//{
//	const int W = text_tex_W;
//	const int H = (int)((1080.0 / 1920) * W);
//	const int x = (int)(uv_x * W);
//	const int y = (int)((1.f - uv_y) * H);
//	
//	return
//		x >= button_left_x && x <= (button_left_x + button_W) &&
//		y >= button_top_y && y <= (button_top_y + button_H);
//}
//
//
//static const std::string makeDataURL(const std::string& html)
//{
//	std::string html_base64;
//	Base64::encode(html.data(), html.size(), html_base64);
//
//	return "data:text/html;base64," + html_base64;
//}





#if CEF_SUPPORT


static CefBrowserHost::MouseButtonType convertToCEFMouseButton(MouseButton button)
{
	if(button == MouseButton::Left)
		return MBT_LEFT;
	else if(button == MouseButton::Right)
		return MBT_RIGHT;
	else if(button == MouseButton::Middle)
		return MBT_MIDDLE;
	else
		return MBT_LEFT;
}


static uint32 convertToCEFModifiers(uint32 modifiers)
{
	uint32 m = 0;
	if(BitUtils::isBitSet(modifiers, (uint32)Modifiers::Shift))	m |= EVENTFLAG_SHIFT_DOWN;
	if(BitUtils::isBitSet(modifiers, (uint32)Modifiers::Ctrl))	m |= EVENTFLAG_CONTROL_DOWN;
	if(BitUtils::isBitSet(modifiers, (uint32)Modifiers::Alt))	m |= EVENTFLAG_ALT_DOWN;
	return m;
}


static uint32 convertToCEFModifiers(uint32 modifiers, MouseButton mouse_button)
{
	uint32 m = convertToCEFModifiers(modifiers);

	if(BitUtils::isBitSet(mouse_button, MouseButton::Left))  m |= EVENTFLAG_LEFT_MOUSE_BUTTON;
	if(BitUtils::isBitSet(mouse_button, MouseButton::Right)) m |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
	return m;
}


#endif // CEF_SUPPORT


void EmbeddedBrowser::mouseReleased(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseReleased()");
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
	{
		if(e->button == MouseButton::Back) // bottom thumb button.  Not a CEF mouse button option, so handle explicitly.  Nothing to do for mouse-up
		{}
		else if(e->button == MouseButton::Forward) // top thumb button.  Nothing to do for mouse-up
		{}
		else
		{
			embedded_cef_browser->sendMouseClickEvent(convertToCEFMouseButton(e->button), uv_coords.x, uv_coords.y, /*mouse_up=*/true, convertToCEFModifiers(e->modifiers, e->button/*e->buttons()*/)); // TEMP REFACTOR using button not buttons
		}
	}
#endif
}


void EmbeddedBrowser::mousePressed(MouseEvent* e, const Vec2f& uv_coords)
{
	//if(showing_click_to_load_text && uvsAreOnLoadButton(uv_coords.x, uv_coords.y))
		//user_clicked_to_load = true;

	//conPrint("mousePressed()");
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
	{
		if(e->button == MouseButton::Back) // bottom thumb button.  Not a CEF mouse button option, so handle explicitly.
		{
			embedded_cef_browser->sendBackMousePress();
		}
		else if(e->button == MouseButton::Forward) // top thumb button
		{
			embedded_cef_browser->sendForwardsMousePress();
		}
		else
		{
			embedded_cef_browser->sendMouseClickEvent(convertToCEFMouseButton(e->button), uv_coords.x, uv_coords.y, /*mouse_up=*/false, convertToCEFModifiers(e->modifiers, e->button)); // TEMP REFACTOR using button not buttons
		}
	}
#endif
}


void EmbeddedBrowser::mouseDoubleClicked(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseDoubleClicked()");
}


void EmbeddedBrowser::mouseMoved(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseMoved(), uv_coords: " + uv_coords.toString());
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->sendMouseMoveEvent(uv_coords.x, uv_coords.y, convertToCEFModifiers(e->modifiers, e->button)); // TEMP REFACTOR using button not buttons
#endif
}


void EmbeddedBrowser::wheelEvent(MouseWheelEvent* e, const Vec2f& uv_coords)
{
	//conPrint("wheelEvent(), uv_coords: " + uv_coords.toString());
#if CEF_SUPPORT
	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->sendMouseWheelEvent(uv_coords.x, uv_coords.y, e->angle_delta.x, e->angle_delta.y, convertToCEFModifiers(e->modifiers/*, e->button*/)); // TEMP REFACTOR using button not buttons
#endif
}


void EmbeddedBrowser::keyPressed(KeyEvent* e)
{
#if CEF_SUPPORT
	const uint32 modifiers = convertToCEFModifiers(e->modifiers);

	// This song and dance of sending two events seems to be needed to type both punctuation and alphabetic characters.

	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->sendKeyEvent(KEYEVENT_RAWKEYDOWN, e->key, e->native_virtual_key, modifiers);

	//if(!e->text.empty())
	//{
	//	//conPrint(QtUtils::toStdString(e->text()));
	//	if(embedded_cef_browser.nonNull())
	//		embedded_cef_browser->sendKeyEvent(KEYEVENT_CHAR, e->text[0]/*e->text().at(0).toLatin1()*/, e->native_virtual_key, modifiers);
	//}
#endif
}


void EmbeddedBrowser::keyReleased(KeyEvent* e)
{
#if CEF_SUPPORT
	const uint32 modifiers = convertToCEFModifiers(e->modifiers);

	if(embedded_cef_browser.nonNull())
		embedded_cef_browser->sendKeyEvent(KEYEVENT_KEYUP, e->key, e->native_virtual_key, modifiers);
#endif
}

void EmbeddedBrowser::handleTextInputEvent(TextInputEvent& e)
{
#if CEF_SUPPORT
	if(!e.text.empty())
	{
		const uint32 modifiers = 0;//convertToCEFModifiers(e.modifiers);

		//conPrint(QtUtils::toStdString(e->text()));
		if(embedded_cef_browser.nonNull())
			embedded_cef_browser->sendKeyEvent(KEYEVENT_CHAR, e.text[0]/*e->text().at(0).toLatin1()*/, e.text[0]/*e.native_virtual_key*/, modifiers);
	}
#endif
}
