/*=====================================================================
EmscriptenResourceDownloader.h
------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "DownloadingResourceQueue.h"
#include <utils/RefCounted.h>
#include <utils/ThreadSafeQueue.h>
#include <set>
#include <string>
class ResourceManager;
class ThreadMessage;
namespace glare { class AtomicInt; }
class DownloadingResourceQueue;
class EmscriptenResourceDownloader;
class GUIClient;


struct CurrentlyDownloadingResource : public RefCounted
{
	URLString URL;
	EmscriptenResourceDownloader* resource_downloader;
	int request_handle;
};


/*=====================================================================
EmscriptenResourceDownloader
----------------------------
Since downloads with emscripten are async, using emscripten_async_wget2,
we don't need multi-threading with DownloadResourceThreads.
So avoid using threads and just call think() every frame.
=====================================================================*/
class EmscriptenResourceDownloader
{
public:
	EmscriptenResourceDownloader();
	~EmscriptenResourceDownloader();

	void init(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<ResourceManager> resource_manager, const std::string& hostname, int port,
		glare::AtomicInt* num_resources_downloading_, DownloadingResourceQueue* download_queue_, GUIClient* gui_client_);

	void shutdown();

	void think();


	void onResourceLoad(Reference<CurrentlyDownloadingResource> res, void* buffer, unsigned int buffer_size_B);
	void onResourceError(Reference<CurrentlyDownloadingResource> res);
private:
	GUIClient* gui_client;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	Reference<ResourceManager> resource_manager;
	std::string hostname;
	int port;
	glare::AtomicInt* num_resources_downloading;
	DownloadingResourceQueue* download_queue;

	std::set<Reference<CurrentlyDownloadingResource>> downloading_resources;
};
