/*=====================================================================
EmscriptenResourceDownloader.cpp
--------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "EmscriptenResourceDownloader.h"


#include "../shared/ResourceManager.h"
#include "DownloadingResourceQueue.h"
#include "DownloadResourcesThread.h" // For ResourceDownloadedMessage
#include "ThreadMessages.h"
#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#if defined(EMSCRIPTEN)
#include <emscripten/emscripten.h>
#endif


EmscriptenResourceDownloader::EmscriptenResourceDownloader()
:	out_msg_queue(NULL),
	port(0),
	num_resources_downloading(NULL),
	download_queue(NULL)
{
}


EmscriptenResourceDownloader::~EmscriptenResourceDownloader()
{
}


void EmscriptenResourceDownloader::init(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_, const std::string& hostname_, int port_,
	glare::AtomicInt* num_resources_downloading_, DownloadingResourceQueue* download_queue_)
{
	out_msg_queue = out_msg_queue_;
	hostname = hostname_;
	resource_manager = resource_manager_;
	port = port_;
	num_resources_downloading = num_resources_downloading_;
	download_queue = download_queue_;
}


void EmscriptenResourceDownloader::shutdown()
{
	// Abort any current async wget requests
	for(auto it = downloading_resources.begin(); it != downloading_resources.end(); ++it)
	{
#if EMSCRIPTEN
		CurrentlyDownloadingResource* downloading_resource = it->ptr();
		emscripten_async_wget2_abort(downloading_resource->request_handle);
#endif
	}

	downloading_resources.clear();
}


#if EMSCRIPTEN

static void onLoad(unsigned int firstarg, void* userdata_arg, const char* filename)
{
	// conPrint("DownloadResourcesThread: onLoad: " + std::string(filename) + ", firstarg: " + toString(firstarg));

	Reference<CurrentlyDownloadingResource> downloading_resource((CurrentlyDownloadingResource*)userdata_arg);

	downloading_resource->resource_downloader->onResourceLoad(downloading_resource);
	
}


static void onError(unsigned int, void* userdata_arg, int http_status_code)
{
	// conPrint("DownloadResourcesThread: onError: " + toString(http_status_code));

	Reference<CurrentlyDownloadingResource> downloading_resource((CurrentlyDownloadingResource*)userdata_arg);

	downloading_resource->resource_downloader->onResourceError(downloading_resource);
}


static void onProgress(unsigned int, void* userdata_arg, int percent_complete)
{
	// conPrint("DownloadResourcesThread: onProgress: " + toString(percent_complete));
}

#endif // EMSCRIPTEN


void EmscriptenResourceDownloader::onResourceLoad(Reference<CurrentlyDownloadingResource> downloading_resource)
{
	if(resource_manager.isNull()) // If not initialised:
		return;

	downloading_resources.erase(downloading_resource);
	(*this->num_resources_downloading)--;

	ResourceRef resource = resource_manager->getOrCreateResourceForURL(downloading_resource->URL);

	resource->setState(Resource::State_Present);
	resource_manager->markAsChanged();

	out_msg_queue->enqueue(new ResourceDownloadedMessage(downloading_resource->URL)); // Send message back to GUIClient
}


void EmscriptenResourceDownloader::onResourceError(Reference<CurrentlyDownloadingResource> downloading_resource)
{
	if(resource_manager.isNull()) // If not initialised:
		return;

	downloading_resources.erase(downloading_resource);
	(*this->num_resources_downloading)--;

	resource_manager->addToDownloadFailedURLs(downloading_resource->URL);

	ResourceRef resource = resource_manager->getOrCreateResourceForURL(downloading_resource->URL);

	resource->setState(Resource::State_NotPresent);

	//conPrint("DownloadResourcesThread: Server couldn't send file '" + URL + "'");// (Result=" + toString(result) + ")");
	out_msg_queue->enqueue(new LogMessage("Server couldn't send resource '" + downloading_resource->URL + "' (resource not found)")); // Send message back to GUIClient
}


void EmscriptenResourceDownloader::think()
{
	const int max_num_concurrent_downloads = 10;

	if(downloading_resources.size() < max_num_concurrent_downloads)
	{
		DownloadQueueItem item;
		const bool got_item = download_queue->tryDequeueItem(item);
		if(got_item)
		{
			if(!resource_manager->isInDownloadFailedURLs(item.URL)) // Don't try to re-download if we already failed to download this session.
			{
				const std::string URL = item.URL;
				ResourceRef resource = resource_manager->getOrCreateResourceForURL(URL);

				if(resource->getState() == Resource::State_NotPresent)
				{
					Reference<CurrentlyDownloadingResource> downloading_resource = new CurrentlyDownloadingResource();
					downloading_resource->resource_downloader = this;
					downloading_resource->URL = item.URL;
					downloading_resources.insert(downloading_resource);

					(*this->num_resources_downloading)++;

					const bool use_TLS = hostname != "localhost"; // Don't use TLS on localhost for now, for testing.
					const std::string protocol = use_TLS ? "https" : "http";
					const std::string http_URL = protocol + "://" + hostname + "/resource/" + URL;
					
					// conPrint("Calling emscripten_wget_data for URL '" + http_URL + "'...");

					const std::string local_abs_path = resource_manager->getLocalAbsPathForResource(*resource);

#if EMSCRIPTEN
					downloading_resource->request_handle = emscripten_async_wget2(http_URL.c_str(), local_abs_path.c_str(), /*requesttype =*/"GET", /*POST params=*/"", 
						/*userdata arg=*/downloading_resource.ptr(), onLoad, onError, onProgress);
#endif
				}
			}
		}
	}
}
