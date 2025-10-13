/*=====================================================================
CEFInternal.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "CEFInternal.h"


#include <ConPrint.h>
#if CEF_SUPPORT  // CEF_SUPPORT will be defined in CMake (or not).
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>
#endif


#if CEF_SUPPORT


LifeSpanHandler::LifeSpanHandler()
{
}


LifeSpanHandler::~LifeSpanHandler()
{
}


bool LifeSpanHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
	bool* no_javascript_access)
{
	CEF_REQUIRE_UI_THREAD();

	conPrint("Page wants to open a popup: " + std::string(target_url));

	// If this was an explicit click on a link, just visit the popup link directly, since we don't want to open in a new tab or window.
	if(user_gesture) // user_gesture is true if the popup was opened via explicit user gesture.
	{
		if(browser && browser->GetHost())
		{
			browser->GetMainFrame()->LoadURL(target_url);
		}
	}

	return true; // "To cancel creation of the popup browser return true"
}


void LifeSpanHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
	CEF_REQUIRE_UI_THREAD();

	mBrowserList.push_back(browser);
}


void LifeSpanHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
	CEF_REQUIRE_UI_THREAD();

	BrowserList::iterator bit = mBrowserList.begin();
	for(; bit != mBrowserList.end(); ++bit)
	{
		if((*bit)->IsSame(browser))
		{
			mBrowserList.erase(bit);
			break;
		}
	}
}


#endif // CEF_SUPPORT
