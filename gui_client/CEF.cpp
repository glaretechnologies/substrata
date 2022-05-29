/*=====================================================================
CEF.cpp
-------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "CEF.h"


#include "CEFInternal.h"
#include <PlatformUtils.h>
#if CEF_SUPPORT  // CEF_SUPPORT will be defined in CMake (or not).
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>
#ifdef OSX
#include <wrapper/cef_library_loader.h>
#endif
#endif
#include "superluminal/PerformanceAPI.h"


#if CEF_SUPPORT


class GlareCEFApp : public CefApp, public RefCounted
{
public:
	GlareCEFApp()
	{
		lifespan_handler = new LifeSpanHandler();
	}

	~GlareCEFApp()
	{
	}

	virtual void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override
	{
		// To allow autoplaying videos etc. without having to click:
		// See https://www.magpcss.org/ceforum/viewtopic.php?f=6&t=16517
		command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

		// On Mac, we get a message box popping up saying "gui_client wants to use your confidential information stored in "Chromium Safe Storage" in your keychain."
		// every time a browser process starts, unless we have this switch.  See https://bitbucket.org/chromiumembedded/cef/issues/2692/mac-networkservice-allow-custom-service#comment-52655833
		// "This prompt can be disabled and cookies will not be encrypted if you pass the --use-mock-keychain command-line flag."
#ifdef OSX
		command_line->AppendSwitch("use-mock-keychain");
#endif

		// command_line->AppendSwitch("disable-gpu");
		// command_line->AppendSwitch("disable-gpu-compositing");

		//TEMP:
		//command_line->AppendSwitch("enable-logging");
		//command_line->AppendSwitchWithValue("v", "2");

		/*if(process_type.empty())
		{
		command_line->AppendSwitch("disable-gpu");
		command_line->AppendSwitch("disable-gpu-compositing");
		}*/
	}

	CefRefPtr<LifeSpanHandler> lifespan_handler;
	
	IMPLEMENT_REFCOUNTING(GlareCEFApp);
};


static bool CEF_initialised = false;
CefRefPtr<GlareCEFApp> glare_cef_app;


bool CEF::isInitialised()
{
	return CEF_initialised;
}


void CEF::initialiseCEF(const std::string& base_dir_path)
{
	assert(!CEF_initialised);
	if(CEF_initialised)
		return;

#ifdef OSX
	// Load the CEF framework library at runtime instead of linking directly
	// as required by the macOS sandbox implementation.
	CefScopedLibraryLoader library_loader;
	if(!library_loader.LoadInMain())
	{
		conPrint("CefScopedLibraryLoader LoadInMain failed.");
		throw glare::Exception("CefScopedLibraryLoader LoadInMain failed.");
	}
#endif

	CefMainArgs args;

	CefSettings settings;
	settings.log_severity = LOGSEVERITY_DISABLE; // Disable writing to logfile on disk (and to stderr), apart from FATAL messages.

#if defined(OSX)
	//const std::string browser_process_path = base_dir_path + "/../Frameworks/gui_client Helper.app"; // On mac, base_dir_path is the path to Resources.
#elif defined(_WIN32)
	settings.no_sandbox = true;
	const std::string browser_process_path = base_dir_path + "/browser_process.exe";
	conPrint("Using browser_process_path '" + browser_process_path + "'...");
	CefString(&settings.browser_subprocess_path).FromString(browser_process_path);

#else // else Linux:
	const std::string browser_process_path = base_dir_path + "/browser_process";
	conPrint("Using browser_process_path '" + browser_process_path + "'...");
	CefString(&settings.browser_subprocess_path).FromString(browser_process_path);
#endif

	glare_cef_app = new GlareCEFApp();

	bool result = CefInitialize(args, settings, glare_cef_app, /*windows sandbox info=*/NULL);
	if(result)
		CEF_initialised = true;
	else
	{
		conPrint("CefInitialize failed.");
	}
}


void CEF::shutdownCEF()
{
	if(CEF_initialised && glare_cef_app)
	{
		// Wait until browser processes are shut down
		while(!glare_cef_app->lifespan_handler->mBrowserList.empty())
		{
			PlatformUtils::Sleep(1);
			CefDoMessageLoopWork();
		}

		glare_cef_app->lifespan_handler = CefRefPtr<LifeSpanHandler>();

		CEF_initialised = false;
		CefShutdown();

		glare_cef_app = CefRefPtr<GlareCEFApp>();
	}
}


LifeSpanHandler* CEF::getLifespanHandler()
{
	return glare_cef_app->lifespan_handler.get();
}


void CEF::doMessageLoopWork()
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

	if(CEF_initialised)
		CefDoMessageLoopWork();
}


#else // else if !CEF_SUPPORT:


bool CEF::isInitialised()
{
	return false;
}


void CEF::initialiseCEF(const std::string& base_dir_path)
{}


void CEF::shutdownCEF()
{}


LifeSpanHandler* CEF::getLifespanHandler()
{
	return NULL;
}


void CEF::doMessageLoopWork()
{}


#endif // end if !CEF_SUPPORT
