/*=====================================================================
AnimatedTextureManager.cpp
--------------------------
Copyright Glare Technologies Limited 2022-
=====================================================================*/
#include "AnimatedTextureManager.h"


#include "MainWindow.h"
#include "../shared/WorldObject.h"
#include "../qt/QtUtils.h"
#include "FileInStream.h"
#include "CEFInternal.h"
#include "CEF.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <utils/Base64.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <Escaping.h>
#include <xxhash.h>


#if CEF_SUPPORT


// This class is shared among all browser instances, the browser the callback applies to is passed in as arg 0.
class AnimatedTexRenderHandler : public CefRenderHandler
{
public:
	AnimatedTexRenderHandler(Reference<OpenGLTexture> opengl_tex_) : opengl_tex(opengl_tex_), opengl_engine(NULL), main_window(NULL), ob(NULL) {}

	~AnimatedTexRenderHandler() {}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
	void onWebViewDataDestroyed()
	{
		opengl_tex = NULL;
		opengl_engine = NULL;
		ob = NULL;
	}

	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override
	{
		CEF_REQUIRE_UI_THREAD();

		rect = CefRect(0, 0, (int)opengl_tex->xRes(), (int)opengl_tex->yRes());
	}

	// "|buffer| will be |width|*|height|*4 bytes in size and represents a BGRA image with an upper-left origin"
	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirty_rects, const void* buffer, int width, int height) override
	{
		CEF_REQUIRE_UI_THREAD();

		//conPrint("OnPaint()");

		if(opengl_engine && ob)
		{
			if(type == PET_VIEW) // whole page was updated
			{
				const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
				if(ob_visible)
				{
					for(size_t i=0; i<dirty_rects.size(); ++i)
					{
						const CefRect& rect = dirty_rects[i];

						//conPrint("Updating dirty rect " + toString(rect.x) + ", " + toString(rect.y) + ", w: " + toString(rect.width) + ", h: " + toString(rect.height));

						// Copy dirty rect data into a packed buffer

						const uint8* start_px = (uint8*)buffer + (width * 4) * rect.y + 4 * rect.x;
						opengl_tex->loadRegionIntoExistingTexture(rect.x, rect.y, rect.width, rect.height, /*row stride (B) = */width * 4, ArrayRef<uint8>(start_px, rect.width * rect.height * 4));
					}
				}
			}
		}
	}

	Reference<OpenGLTexture> opengl_tex;
	OpenGLEngine* opengl_engine;
	MainWindow* main_window;
	WorldObject* ob;

	IMPLEMENT_REFCOUNTING(AnimatedTexRenderHandler);
};


// Reads a resource off disk and returns the data to CEF.
class AnimatedTexResourceHandler : public CefResourceHandler
{
public:
	AnimatedTexResourceHandler(Reference<ResourceManager> resource_manager_) : file_in_stream(NULL), resource_manager(resource_manager_) {}
	~AnimatedTexResourceHandler() { delete file_in_stream; }

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

		if(hasPrefix(URL, "http://resource/"))
		{
			const std::string resource_URL = eatPrefix(URL, "http://resource/");
			//conPrint("resource_URL: " + resource_URL);

			ResourceRef resource = resource_manager->getExistingResourceForURL(resource_URL);
			if(resource.nonNull())
			{
				const std::string path = resource->getLocalPath();
				//conPrint("path: " + path);
				try
				{
					file_in_stream = new FileInStream(path);

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
		if(file_in_stream)
		{
			response_length = (int64)this->file_in_stream->fileSize();
		}
		else
		{
			response_length = -1;
		}
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
		if(file_in_stream)
		{
			try
			{
				file_in_stream->setReadIndex(file_in_stream->getReadIndex() + bytes_to_skip);
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
		if(file_in_stream)
		{
			try
			{
				const size_t bytes_available = file_in_stream->fileSize() - file_in_stream->getReadIndex(); // bytes still available to read in the file
				if(bytes_available == 0)
				{
					bytes_read = 0; // indicate response completion (EOF)
					return false;
				}
				else
				{
					const size_t use_bytes_to_read = myMin((size_t)bytes_to_read, bytes_available);
					file_in_stream->readData(data_out, use_bytes_to_read);
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

	FileInStream* file_in_stream;
	Reference<ResourceManager> resource_manager;

	IMPLEMENT_REFCOUNTING(AnimatedTexResourceHandler);
};



class AnimatedTexCefClient : public CefClient, public CefRequestHandler, public CefLoadHandler, public CefDisplayHandler, public CefAudioHandler, public CefCommandHandler, public CefContextMenuHandler, public CefResourceRequestHandler
{
public:
	AnimatedTexCefClient() : num_channels(0), sample_rate(0), main_window(NULL), ob(NULL) {}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
	void onWebViewDataDestroyed()
	{
		main_window = NULL;
		ob = NULL;
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return mRenderHandler; }

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return mLifeSpanHandler; }

	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

	CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }

	CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }

	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }


	virtual bool OnCursorChange(CefRefPtr<CefBrowser> browser,
		CefCursorHandle cursor,
		cef_cursor_type_t type,
		const CefCursorInfo& custom_cursor_info) override
	{
		return false;
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

	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool user_gesture,
		bool is_redirect) override

	{
		return false;
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

		if(hasPrefix(request->GetURL().ToString(), "http://resource/"))
		{
			// conPrint("interecepting resource request");
			disable_default_handling = true;
			return this;
		}
		return nullptr;
	}

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


	virtual void OnStatusMessage(CefRefPtr<CefBrowser> browser,
		const CefString& value) override
	{
		if(value.c_str())
		{
			//conPrint("OnStatusMessage: " + StringUtils::PlatformToUTF8UnicodeEncoding(value.c_str()));

			if(main_window)
				main_window->webViewDataLinkHovered(QtUtils::toQString(value.ToString()));
			//if(web_view_data)
			//	web_view_data->linkHoveredSignal(QtUtils::toQString(value.ToString()));
		}
		else
		{
			//conPrint("OnStatusMessage: NULL");

			//if(web_view_data)
			//	web_view_data->linkHoveredSignal("");
			if(main_window)
				main_window->webViewDataLinkHovered("");
		}
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

		// TODO: check URL prefix for http://resource/, return new AnimatedTexResourceHandler only in that case?
		// Will we need to handle other URLS here?

		return new AnimatedTexResourceHandler(main_window->resource_manager);
		//return nullptr;
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
	}


	//--------------------- AudioHandler ----------------------------
	// Called on the UI thread to allow configuration of audio stream parameters.
	// Return true to proceed with audio stream capture, or false to cancel it.
	// All members of |params| can optionally be configured here, but they are
	// also pre-filled with some sensible defaults.
	virtual bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
		CefAudioParameters& params) override 
	{
		// conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": GetAudioParameters().");

		params.sample_rate = main_window->audio_engine.getSampleRate();

		// Create audio source.  Do this now while we're in the main (UI) thread.
		if(ob && ob->audio_source.isNull())
		{
			// conPrint("OnAudioStreamStarted(), creating audio src");

			// Create a streaming audio source.
			this->audio_source = new glare::AudioSource(); // Hang on to a reference we can use from the audio stream thread.
			audio_source->type = glare::AudioSource::SourceType_Streaming;
			audio_source->pos = ob->aabb_ws.centroid();
			audio_source->debugname = "animated tex: " + ob->target_url;

			{
				Lock lock(main_window->world_state->mutex);

				const Parcel* parcel = main_window->world_state->getParcelPointIsIn(ob->pos);
				audio_source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.
			}

			ob->audio_source = audio_source;
			main_window->audio_engine.addSource(audio_source);
		}

		return true;
	}

	// Called on a browser audio capture thread when the browser starts
	// streaming audio.
	virtual void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
		const CefAudioParameters& params,
		int channels) override
	{
		// conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": OnAudioStreamStarted().");
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
		if(main_window && this->audio_source.nonNull())
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


			{
				Lock mutex(main_window->audio_engine.mutex);
				this->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
			}
		}
	}

	virtual void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override
	{
		/*if(ob && ob->audio_source.nonNull())
		{
			main_window->audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
		}*/
	}

	virtual void OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString& message) override
	{
		conPrint("=========================================\nOnAudioStreamError: " + message.ToString());
	}
	// ----------------------------------------------------------------------------


	CefRefPtr<AnimatedTexRenderHandler> mRenderHandler;
	CefRefPtr<CefLifeSpanHandler> mLifeSpanHandler; // The lifespan handler has references to the CefBrowsers, so the browsers should 

	MainWindow* main_window;
	WorldObject* ob;
	Reference<glare::AudioSource> audio_source;
	int num_channels;
	int sample_rate;

	std::vector<float> temp_buf;

	IMPLEMENT_REFCOUNTING(AnimatedTexCefClient);
};




class AnimatedTexCEFBrowser : public RefCounted
{
public:
	AnimatedTexCEFBrowser(AnimatedTexRenderHandler* render_handler, LifeSpanHandler* lifespan_handler)
	:	mRenderHandler(render_handler)
	{
		cef_client = new AnimatedTexCefClient();
		cef_client->mRenderHandler = mRenderHandler;
		cef_client->mLifeSpanHandler = lifespan_handler;
	}

	virtual ~AnimatedTexCEFBrowser()
	{
	}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
	void onWebViewDataDestroyed()
	{
		// conPrint("AnimatedTexCEFBrowser::onWebViewDataDestroyed()");

		cef_client->onWebViewDataDestroyed();
		mRenderHandler->onWebViewDataDestroyed();
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

	CefRefPtr<AnimatedTexRenderHandler> mRenderHandler;
	CefRefPtr<CefBrowser> cef_browser;
	CefRefPtr<AnimatedTexCefClient> cef_client;
};



static Reference<AnimatedTexCEFBrowser> createBrowser(const std::string& URL, Reference<OpenGLTexture> opengl_tex)
{
	// conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": creating browser.");

	Reference<AnimatedTexCEFBrowser> browser = new AnimatedTexCEFBrowser(new AnimatedTexRenderHandler(opengl_tex), CEF::getLifespanHandler());

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

class AnimatedTexCEFBrowser : public RefCounted
{};

#endif // CEF_SUPPORT


AnimatedTexData::AnimatedTexData()
:	last_loaded_frame_i(-1),
	cur_frame_i(0)
{}

AnimatedTexData::~AnimatedTexData()
{
#if CEF_SUPPORT
	if(browser.nonNull())
	{
		browser->onWebViewDataDestroyed(); // The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
		browser->requestExit();
		browser = NULL;
	}
#endif
}


static const std::string makeDataURL(const std::string& html)
{
	std::string html_base64;
	Base64::encode(html.data(), html.size(), html_base64);

	return "data:text/html;base64," + html_base64;
}


void AnimatedTexObData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	if(ob->opengl_engine_ob.isNull())
		return;

#if CEF_SUPPORT

	const double ob_dist_from_cam = ob->pos.getDist(main_window->cam_controller.getPosition());
	const double max_play_dist = AnimatedTexData::maxVidPlayDist();
	const bool in_process_dist = ob_dist_from_cam < max_play_dist;

	const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);

	if(in_process_dist)
	{
		AnimatedTexObData& animation_data = *this;
		animation_data.mat_animtexdata.resize(ob->opengl_engine_ob->materials.size());

		for(size_t m=0; m<ob->opengl_engine_ob->materials.size(); ++m)
		{
			OpenGLMaterial& mat = ob->opengl_engine_ob->materials[m];
			
			if(mat.albedo_texture.nonNull() && hasExtensionStringView(mat.tex_path, "gif"))
			{
				if(animation_data.mat_animtexdata[m].isNull())
					animation_data.mat_animtexdata[m] = new AnimatedTexData();
				AnimatedTexData& animtexdata = *animation_data.mat_animtexdata[m];

				// Fetch the texdata for this texture if we haven't already
				// Note that mat.tex_path may change due to LOD changes.
				if(animtexdata.texdata.isNull() || (animtexdata.texdata_tex_path != mat.tex_path))
				{
					//if(animtexdata.texdata_tex_path != mat.tex_path)
					//	conPrint("AnimatedTexObData::process(): tex_path changed from '" + animtexdata.texdata_tex_path + "' to '" + mat.tex_path + "'.");

					// Only replace the tex data if the new tex data is non-null.
					// new tex data may take a while to load after LOD changes.
					Reference<TextureData> new_tex_data = opengl_engine->texture_data_manager->getTextureData(mat.tex_path);
					if(new_tex_data.nonNull())
					{
						animtexdata.texdata = new_tex_data;
						animtexdata.texdata_tex_path = mat.tex_path;
					}
				}

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
					{
						if(ob_visible && animtexdata.cur_frame_i != animtexdata.last_loaded_frame_i) // TODO: avoid more work when object is not visible.
						{
							//printVar(animtexdata.cur_frame_i);

							// There may be some frames after a LOD level change where the new texture with the updated size is loading, and thus we have a size mismatch.
							// Don't try and upload the wrong size or we will get an OpenGL error or crash.
							if(mat.albedo_texture->xRes() == texdata->W && mat.albedo_texture->yRes() == texdata->H)
								TextureLoading::loadIntoExistingOpenGLTexture(mat.albedo_texture, *texdata, animtexdata.cur_frame_i, opengl_engine);
							//else
							//	conPrint("AnimatedTexObData::process(): tex data W or H wrong.");

							animtexdata.last_loaded_frame_i = animtexdata.cur_frame_i;
						}
					}
				}
			}
			else if(hasExtensionStringView(mat.tex_path, "mp4"))
			{
				if(animation_data.mat_animtexdata[m].isNull())
					animation_data.mat_animtexdata[m] = new AnimatedTexData();
				AnimatedTexData& animtexdata = *animation_data.mat_animtexdata[m];

				if(!CEF::isInitialised())
				{
					CEF::initialiseCEF(main_window->base_dir_path);
				}

				if(CEF::isInitialised())
				{
					if(animtexdata.browser.isNull() && !mat.tex_path.empty() && ob->opengl_engine_ob.nonNull())
					{
						main_window->logMessage("Creating browser to play vid, URL: " + mat.tex_path);

						const int width = 1024;
						const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
						const int height = myClamp((int)(1024 * use_height_over_width), 16, 2048);

						if(ob->opengl_engine_ob.nonNull())
						{
							std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
							mat.albedo_texture = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTexture::Format_SRGBA_Uint8,
								GL_SRGB8_ALPHA8, // GL internal format
								GL_BGRA, // GL format.
								OpenGLTexture::Filtering_Bilinear);

							mat.fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
						}

						ResourceRef resource = main_window->resource_manager->getExistingResourceForURL(mat.tex_path);

						// if the resource is downloaded already, read video off disk:
						std::string use_URL;
						if(resource.nonNull() && resource->getState() == Resource::State_Present)
						{
							use_URL = "http://resource/" + web::Escaping::URLEscape(mat.tex_path);// resource->getLocalPath();
						}
						else // Otherwise use streaming via HTTP
						{
							if(hasPrefix(mat.tex_path, "http://") || hasPrefix(mat.tex_path, "https://"))
							{
								use_URL = mat.tex_path;
							}
							else
							{
								// If the URL does not have an HTTP prefix (e.g. is just a normal resource URL), rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
								use_URL = "http://" + main_window->server_hostname + "/resource/" + web::Escaping::URLEscape(mat.tex_path);
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

						Reference<AnimatedTexCEFBrowser> browser = createBrowser(data_URL, ob->opengl_engine_ob->materials[0].albedo_texture);
						browser->cef_client->main_window = main_window;
						browser->cef_client->ob = ob;
						browser->mRenderHandler->opengl_engine = opengl_engine;
						browser->mRenderHandler->main_window = main_window;
						browser->mRenderHandler->ob = ob;

						animtexdata.browser = browser;

						animtexdata.texdata_tex_path = mat.tex_path;
					}
				}
			}
		}
	}
	else // else if !in_process_dist:
	{
		// Close any browsers for animated textures on this object.
		for(size_t m=0; m<this->mat_animtexdata.size(); ++m)
		{
			if(this->mat_animtexdata[m].nonNull())
			{
				AnimatedTexData& animtexdata = *this->mat_animtexdata[m];
				if(animtexdata.browser.nonNull())
				{
					main_window->logMessage("Closing vid playback browser (out of view distance).");
					animtexdata.browser->requestExit();
					animtexdata.browser = NULL;

					// Remove audio source
					if(ob->audio_source.nonNull())
					{
						main_window->audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
					}
				}
			}
		}
	}
#endif // CEF_SUPPORT
}
