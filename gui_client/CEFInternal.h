/*=====================================================================
CEFInternal.h
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#if CEF_SUPPORT  // CEF_SUPPORT will be defined in CMake (or not).
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>
#ifdef OSX
#include <wrapper/cef_library_loader.h>
#endif
#endif
#include <string>
#include <list>


#if CEF_SUPPORT


// This class is shared among all browser instances, the browser the callback applies to is passed in as arg 0.
class LifeSpanHandler : public CefLifeSpanHandler
{
public:
	LifeSpanHandler();
	virtual ~LifeSpanHandler();

	virtual bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		//int popup_id,
		const CefString& target_url,
		const CefString& target_frame_name,
		WindowOpenDisposition target_disposition,
		bool user_gesture,
		const CefPopupFeatures& popupFeatures,
		CefWindowInfo& windowInfo,
		CefRefPtr<CefClient>& client,
		CefBrowserSettings& settings,
		CefRefPtr<CefDictionaryValue>& extra_info,
		bool* no_javascript_access) override;

	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

	IMPLEMENT_REFCOUNTING(LifeSpanHandler);

public:
	typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
	BrowserList mBrowserList;
};


#endif // CEF_SUPPORT
