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

	CefMainArgs main_args(GetModuleHandle(NULL));

	// Implementation of the CefApp interface.
	CefRefPtr<WebViewDataCEFApp> app = new WebViewDataCEFApp();

	// Execute the sub-process logic. This will block until the sub-process should exit.
	return CefExecuteProcess(main_args, app.get(), /*windows sandbox info=*/NULL);
}
