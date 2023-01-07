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
#include <unistd.h>
#include <sys/inotify.h>
#include <linux/limits.h>
#endif


WebDataFileWatcherThread::WebDataFileWatcherThread(Reference<WebDataStore> web_data_store_)
:	web_data_store(web_data_store_)
{}


WebDataFileWatcherThread::~WebDataFileWatcherThread()
{}


#if defined(_WIN32)
static HANDLE makeWaitHandleForDir(const std::string& dir, bool watch_subtree)
{
	HANDLE wait_handle = FindFirstChangeNotification(StringUtils::UTF8ToPlatformUnicodeEncoding(dir).c_str(), /*watch subtree=*/watch_subtree, FILE_NOTIFY_CHANGE_LAST_WRITE);
	if(wait_handle == INVALID_HANDLE_VALUE)
		throw glare::Exception("FindFirstChangeNotification failed: " + PlatformUtils::getLastErrorString());
	return wait_handle;
}
#elif defined(OSX)
 // TODO
#else // else on Linux:
static int makeWatchDescriptorForDir(const std::string& dir)
{
	const int watch_descriptor = inotify_add_watch(inotify_fd, dir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE);
	if(watch_descriptor == -1)
		throw glare::Exception("inotify_add_watch failed: " + PlatformUtils::getLastErrorString());
	return watch_descriptor;
}
#endif


void WebDataFileWatcherThread::doRun()
{
	try
	{
#if defined(_WIN32)
		std::vector<HANDLE> wait_handles;
		wait_handles.push_back(makeWaitHandleForDir(web_data_store->fragments_dir,		/*watch subtree=*/true));
		wait_handles.push_back(makeWaitHandleForDir(web_data_store->public_files_dir,	/*watch subtree=*/false));
		wait_handles.push_back(makeWaitHandleForDir(web_data_store->webclient_dir,		/*watch subtree=*/true));
		
		while(1)
		{
			const DWORD res = WaitForMultipleObjects(/*count=*/(DWORD)wait_handles.size(), wait_handles.data(), /*wait all=*/FALSE, /*wait time=*/INFINITE);
			if(res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + (DWORD)wait_handles.size())
			{
				const size_t i = res - WAIT_OBJECT_0;
				conPrint("public_files_dir or webclient_dir or fragments_dir file(s) changed, reloading files...");

				web_data_store->loadAndCompressFiles();

				if(FindNextChangeNotification(wait_handles[i]) == 0)
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
		watch_descriptors.push_back(makeWatchDescriptorForDir(web_data_store->fragments_dir));
		watch_descriptors.push_back(makeWatchDescriptorForDir(web_data_store->public_files_dir));
		watch_descriptors.push_back(makeWatchDescriptorForDir(web_data_store->webclient_dir));

		std::vector<uint8> buf(sizeof(struct inotify_event) + NAME_MAX + 1); // We have to read more than just sizeof(struct inotify_event), as the name field extends past end of structure.
		// See https://man7.org/linux/man-pages/man7/inotify.7.html
		while(1)
		{
			const int length = read(inotify_fd, buf.data(), buf.size()); // Do a blocking read call.
			if(length == -1)
				throw glare::Exception("read failed: " + PlatformUtils::getLastErrorString());

			if(length >= (int)sizeof(struct inotify_event))
			{
				conPrint("public_files_dir or webclient_dir or fragments_dir file(s) changed, reloading files...");

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
