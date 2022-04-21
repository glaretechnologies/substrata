/*=====================================================================
BrowserProcess.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


// Used for running the Chromium Embedded Framework code in another process, as is recommended.


#include <cef_app.h>
#include <cef_sandbox_win.h>
#include <iostream>


class WebViewDataCEFApp : public CefApp, public CefBrowserProcessHandler
{
	void initialise()
	{
	}

	// CefApp methods:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

	IMPLEMENT_REFCOUNTING(WebViewDataCEFApp);
};


// See https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage.md#markdown-header-entry-point-function
#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) // use WinMain on Windows, otherwise we get a console window showing when browser_process is run.
#else
int main(int argc, char* argv[])
#endif
{
	// std::cout << "BrowserProcess!!!" << std::endl;
#if defined(_WIN32)
	CefMainArgs main_args(GetModuleHandle(NULL));

#elif defined(OSX)
	CefMainArgs main_args(argc, argv);

	// See https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage#markdown-header-separate-sub-process-executable
	
	// Initialize the macOS sandbox for this helper process.
	CefScopedSandboxContext sandbox_context;
	if(!sandbox_context.Initialize(argc, argv))
		return 1;

	// Load the CEF framework library at runtime instead of linking directly
	// as required by the macOS sandbox implementation.
	CefScopedLibraryLoader library_loader;
	if(!library_loader.LoadInHelper())
		return 1;
#endif

	// Implementation of the CefApp interface.
	CefRefPtr<WebViewDataCEFApp> app = new WebViewDataCEFApp();

	// Execute the sub-process logic. This will block until the sub-process should exit.
	return CefExecuteProcess(main_args, app.get(), /*windows sandbox info=*/NULL);
}
