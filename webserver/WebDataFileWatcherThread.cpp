/*=====================================================================
WebDataFileWatcherThread.cpp
----------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebDataFileWatcherThread.h"


#include <MemMappedFile.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <PlatformUtils.h>
#include <Lock.h>
#include <ResponseUtils.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif


WebDataFileWatcherThread::WebDataFileWatcherThread(Reference<WebDataStore> web_data_store_)
:	web_data_store(web_data_store_)
{}


WebDataFileWatcherThread::~WebDataFileWatcherThread()
{}


void WebDataFileWatcherThread::doRun()
{
	try
	{
#if defined(_WIN32)
		std::vector<HANDLE> wait_handles;

		{
			HANDLE wait_handle = FindFirstChangeNotification(StringUtils::UTF8ToPlatformUnicodeEncoding(web_data_store->public_files_dir).c_str(), /*watch subtree=*/FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
			if(wait_handle == INVALID_HANDLE_VALUE)
				throw glare::Exception("FindFirstChangeNotification failed: " + PlatformUtils::getLastErrorString());
			wait_handles.push_back(wait_handle);
		}

		{
			HANDLE wait_handle = FindFirstChangeNotification(StringUtils::UTF8ToPlatformUnicodeEncoding(web_data_store->webclient_dir).c_str(), /*watch subtree=*/TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
			if(wait_handle == INVALID_HANDLE_VALUE)
				throw glare::Exception("FindFirstChangeNotification failed: " + PlatformUtils::getLastErrorString());
			wait_handles.push_back(wait_handle);
		}

		while(1)
		{
			const DWORD res = WaitForMultipleObjects(/*count=*/(DWORD)wait_handles.size(), wait_handles.data(), /*wait all=*/FALSE, /*wait time=*/INFINITE);
			if(res == WAIT_OBJECT_0)
			{
				conPrint("public_files_dir changed, reloading files...");

				web_data_store->loadAndCompressFiles();

				if(FindNextChangeNotification(wait_handles[0]) == 0)
					throw glare::Exception("FindNextChangeNotification failed: " + PlatformUtils::getLastErrorString());
			}
			else if(res == (WAIT_OBJECT_0 + 1))
			{
				conPrint("webclient_dir changed, reloading files...");

				web_data_store->loadAndCompressFiles();

				if(FindNextChangeNotification(wait_handles[1]) == 0)
					throw glare::Exception("FindNextChangeNotification failed: " + PlatformUtils::getLastErrorString());
			}
			else
				throw glare::Exception("Unhandled WaitForSingleObject result");


		}
#elif defined(OSX)
		
		// TODO

#else // else on Linux:

		const int inotify_fd = inotify_init();
		if(inotify_fd == -1)
			throw glare::Exception("inotify_init failed: " + PlatformUtils::getLastErrorString());

		std::vector<int> watch_descriptors;
		{
			const int watch_descriptor = inotify_add_watch(inotify_fd, web_data_store->public_files_dir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE);
			if(watch_descriptor == -1)
				throw glare::Exception("inotify_add_watch failed: " + PlatformUtils::getLastErrorString());
		}
		{
			const int watch_descriptor = inotify_add_watch(inotify_fd, web_data_store->webclient_dir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE);
			if(watch_descriptor == -1)
				throw glare::Exception("inotify_add_watch failed: " + PlatformUtils::getLastErrorString());
		}

		std::vector<uint8> buf(sizeof(struct inotify_event) + NAME_MAX + 1); // We have to read more than just sizeof(struct inotify_event), as the name field extends past end of structure.
		// See https://man7.org/linux/man-pages/man7/inotify.7.html
		while(1)
		{
			const int length = read(inotify_fd, buf.data(), buf.size()); // Do a blocking read call.
			if(length == -1)
				throw glare::Exception("read failed: " + PlatformUtils::getLastErrorString());

			if(length >= (int)sizeof(struct inotify_event))
			{
				conPrint("public_files_dir or webclient_dir file(s) changed, reloading files...");

				// The reload-trigger file has changed in some way.  So reload files
				web_data_store->loadAndCompressFiles();
			}
		}

		for(size_t i=0; i<watch_descriptors.size(); ++i)
		{
			int res = inotify_rm_watch(inotify_fd, watch_descriptors[i]);
			assertOrDeclareUsed(res != -1);
		}

		int res = close(inotify_fd);
		assertOrDeclareUsed(res != -1);

#endif
	}
	catch(glare::Exception& e)
	{
		conPrint("WebDataFileWatcherThread error: " + e.what());
	}
}
