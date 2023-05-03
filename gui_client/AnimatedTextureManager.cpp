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
// The methods of this class will be called on the UI (main) thread.
class AnimatedTexRenderHandler : public CefRenderHandler
{
public:
	AnimatedTexRenderHandler(Reference<OpenGLTexture> opengl_tex_, MainWindow* main_window_, WorldObject* ob_) : opengl_tex(opengl_tex_), opengl_engine(NULL), main_window(main_window_), ob(ob_) {}

	~AnimatedTexRenderHandler() {}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
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
						opengl_tex->loadRegionIntoExistingTexture(/*mip level=*/0, rect.x, rect.y, rect.width, rect.height, /*row stride (B) = */width * 4, ArrayRef<uint8>(start_px, rect.width * rect.height * 4), /*bind_needed=*/true);
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
				const std::string path = resource_manager->getLocalAbsPathForResource(*resource);
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
		//conPrint("Skipping " + toString(bytes_to_skip) + " B...");
		if(file_in_stream)
		{
			try
			{
				// There seems to be a CEF bug where are a Skip call is made at the start of a resource read, after a video repeats.
				// This breaks video looping.  Work around it by detecting the skip call and doing nothing in that case.
				// See https://magpcss.org/ceforum/viewtopic.php?f=6&t=19171
				if(file_in_stream->getReadIndex() != 0)
					file_in_stream->advanceReadIndex(bytes_to_skip);

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
				//conPrint("Reading up to " + toString(bytes_to_read) + " B, getReadIndex: " + toString(file_in_stream->getReadIndex()));
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
	AnimatedTexCefClient(MainWindow* main_window_, WorldObject* ob_) : num_channels(0), sample_rate(0), m_main_window(main_window_), m_ob(ob_) {}

	// The browser will not be destroyed immediately.  NULL out references to object, gl engine etc. because they may be deleted soon.
	void onWebViewDataDestroyed()
	{
		CEF_REQUIRE_UI_THREAD();

		{
			Lock lock(mutex);
			m_main_window = NULL;
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

		Reference<ResourceManager> resouce_manager;
		{
			Lock lock(mutex);
			resouce_manager = this->m_main_window->resource_manager;
		}

		return new AnimatedTexResourceHandler(resouce_manager);
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
		CEF_REQUIRE_UI_THREAD();

		MainWindow* main_window;
		WorldObject* ob;
		{
			Lock lock(mutex);
			main_window = this->m_main_window;
			ob = this->m_ob;
		}

		if(!main_window)
			return false;

		// conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": GetAudioParameters().");

		params.sample_rate = main_window->audio_engine.getSampleRate();

		// Create audio source.  Do this now while we're in the main (UI) thread.
		if(ob && ob->audio_source.isNull())
		{
			// conPrint("OnAudioStreamStarted(), creating audio src");

			// Create a streaming audio source.
			this->audio_source = new glare::AudioSource(); // Hang on to a reference we can use from the audio stream thread.
			audio_source->type = glare::AudioSource::SourceType_Streaming;
			audio_source->pos = ob->getCentroidWS();
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

			// We are accessing main_window, that might have been set to null in another thread via onWebViewDataDestroyed(), so lock mutex.
			// We need to hold this mutex the entire time we using m_main_window, to prevent main_window closing in another thread.
			{
				Lock lock(mutex);
				if(m_main_window)
				{
					Lock audio_engine_lock(m_main_window->audio_engine.mutex);
					this->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
				}
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

	Mutex mutex; // Protects main_window, ob
	MainWindow* m_main_window			GUARDED_BY(mutex);
	WorldObject* m_ob					GUARDED_BY(mutex);

	Reference<glare::AudioSource> audio_source; // Store a direct reference to the audio_source, to make sure it's alive while we are using it.
	int num_channels;
	int sample_rate;

	std::vector<float> temp_buf;

	IMPLEMENT_REFCOUNTING(AnimatedTexCefClient);
};




class AnimatedTexCEFBrowser : public RefCounted
{
public:
	AnimatedTexCEFBrowser(AnimatedTexRenderHandler* render_handler, LifeSpanHandler* lifespan_handler, MainWindow* main_window_, WorldObject* ob_)
	:	mRenderHandler(render_handler)
	{
		cef_client = new AnimatedTexCefClient(main_window_, ob_);
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



static Reference<AnimatedTexCEFBrowser> createBrowser(const std::string& URL, Reference<OpenGLTexture> opengl_tex, MainWindow* main_window, WorldObject* ob)
{
	// conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": creating browser.");

	Reference<AnimatedTexCEFBrowser> browser = new AnimatedTexCEFBrowser(new AnimatedTexRenderHandler(opengl_tex, main_window, ob), CEF::getLifespanHandler(), main_window, ob);

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


void AnimatedTexObData::processGIFAnimatedTex(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
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
						int index = res - texdata->frame_end_times.begin();
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


void AnimatedTexObData::processMP4AnimatedTex(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
	OpenGLMaterial& mat, AnimatedTexData& animtexdata, const std::string& tex_path, bool is_refl_tex)
{
#if CEF_SUPPORT
	if(!CEF::isInitialised())
	{
		CEF::initialiseCEF(main_window->base_dir_path);
	}

	if(CEF::isInitialised())
	{
		if(animtexdata.browser.isNull() && !tex_path.empty() && ob->opengl_engine_ob.nonNull())
		{
			main_window->logMessage("Creating browser to play vid, URL: " + tex_path);

			const int width = 1024;
			const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
			const int height = myClamp((int)(1024 * use_height_over_width), 16, 2048);

			std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
			OpenGLTextureRef new_tex /*mat.albedo_texture*/ = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTexture::Format_SRGBA_Uint8,
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

			ResourceRef resource = main_window->resource_manager->getExistingResourceForURL(tex_path);

			// if the resource is downloaded already, read video off disk:
			std::string use_URL;
			if(resource.nonNull() && resource->getState() == Resource::State_Present)
			{
				use_URL = "http://resource/" + web::Escaping::URLEscape(tex_path);// resource->getLocalPath();
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
					use_URL = "http://" + main_window->server_hostname + "/resource/" + web::Escaping::URLEscape(tex_path);
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

			Reference<AnimatedTexCEFBrowser> browser = createBrowser(data_URL, new_tex, main_window, ob);
			browser->mRenderHandler->opengl_engine = opengl_engine;

			animtexdata.browser = browser;
		}
	}
#endif // CEF_SUPPORT
}


static inline float largestDim(const js::AABBox& aabb)
{
	return horizontalMax((aabb.max_ - aabb.min_).v);
}


AnimatedTexObDataProcessStats AnimatedTexObData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
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

		const float max_mp4_dist = AnimatedTexData::maxVidPlayDist(); // textures <= max_dist are updated
		const float min_mp4_recip_dist = 1 / max_mp4_dist; // textures >= min_recip_dist are updated

		const float ob_w = ob->getAABBWSLongestLength();
		const float recip_dist = (ob->getCentroidWS() - main_window->cam_controller.getPosition().toVec4fPoint()).fastApproxRecipLength();
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

				processGIFAnimatedTex(main_window, opengl_engine, ob, anim_time, dt, mat, mat.albedo_texture, *animation_data.mat_animtexdata[m].refl_col_animated_tex_data, mat.tex_path, /*is refl tex=*/true);
				stats.num_gif_textures_processed++;
			}
			else if(hasExtensionStringView(mat.tex_path, "mp4"))
			{
				if(mp4_large_enough)
				{
					if(animation_data.mat_animtexdata[m].refl_col_animated_tex_data.isNull())
						animation_data.mat_animtexdata[m].refl_col_animated_tex_data = new AnimatedTexData();

					processMP4AnimatedTex(main_window, opengl_engine, ob, anim_time, dt, mat, *animation_data.mat_animtexdata[m].refl_col_animated_tex_data, mat.tex_path, /*is refl tex=*/true);
					stats.num_mp4_textures_processed++;
				}
			}

			//---- Handle animated emission texture ----
			if(mat.emission_texture.nonNull() && hasExtensionStringView(mat.emission_tex_path, "gif"))
			{
				if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
					animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData();

				processGIFAnimatedTex(main_window, opengl_engine, ob, anim_time, dt, mat, mat.emission_texture, *animation_data.mat_animtexdata[m].emission_col_animated_tex_data, mat.emission_tex_path, /*is refl tex=*/false);
				stats.num_gif_textures_processed++;

			}
			else if(hasExtensionStringView(mat.emission_tex_path, "mp4"))
			{
				if(mp4_large_enough)
				{
					if(animation_data.mat_animtexdata[m].emission_col_animated_tex_data.isNull())
						animation_data.mat_animtexdata[m].emission_col_animated_tex_data = new AnimatedTexData();

					processMP4AnimatedTex(main_window, opengl_engine, ob, anim_time, dt, mat, *animation_data.mat_animtexdata[m].emission_col_animated_tex_data, mat.emission_tex_path, /*is refl tex=*/false);
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

			if(this->mat_animtexdata[m].emission_col_animated_tex_data.nonNull())
			{
				AnimatedTexData& animtexdata = *this->mat_animtexdata[m].emission_col_animated_tex_data;
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
#endif
	}

	// If the object is sufficiently far from the camera, clean up gif playback data.
	// NOTE: nothing to do now tex_data for animated textures is stored in the texture?

	return stats;
}
