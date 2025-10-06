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
#include "GUIClient.h"
#include <ConPrint.h>
#include <FileUtils.h>
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
	glare::AtomicInt* num_resources_downloading_, DownloadingResourceQueue* download_queue_, GUIClient* gui_client_)
{
	out_msg_queue = out_msg_queue_;
	hostname = hostname_;
	resource_manager = resource_manager_;
	port = port_;
	num_resources_downloading = num_resources_downloading_;
	download_queue = download_queue_;
	gui_client = gui_client_;
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

// Callback on successful load of the file.
static void onLoad(unsigned int request_handle, void* userdata_arg, void* buffer, unsigned int buffer_size_B)
{
	// conPrint("DownloadResourcesThread: onLoad: " + std::string(filename) + ", firstarg: " + toString(firstarg));

	Reference<CurrentlyDownloadingResource> downloading_resource((CurrentlyDownloadingResource*)userdata_arg);

	downloading_resource->resource_downloader->onResourceLoad(downloading_resource, buffer, buffer_size_B);
	
}


static void onError(unsigned int request_handle, void* userdata_arg, int http_status_code, const char* status_descrip)
{
	// conPrint("DownloadResourcesThread: onError: " + toString(http_status_code));

	Reference<CurrentlyDownloadingResource> downloading_resource((CurrentlyDownloadingResource*)userdata_arg);

	downloading_resource->resource_downloader->onResourceError(downloading_resource);
}


static void onProgress(unsigned int request_handle, void* userdata_arg, int percent_complete, int total_data_size)
{
	// conPrint("DownloadResourcesThread: onProgress: " + toString(percent_complete));
}

#endif // EMSCRIPTEN


void EmscriptenResourceDownloader::onResourceLoad(Reference<CurrentlyDownloadingResource> downloading_resource, void* buffer, unsigned int buffer_size_B)
{
	if(resource_manager.isNull()) // If not initialised:
		return;

	downloading_resources.erase(downloading_resource);
	(*this->num_resources_downloading)--;

	ResourceRef resource = resource_manager->getOrCreateResourceForURL(downloading_resource->URL);
	resource->setState(Resource::State_Present);

	resource_manager->total_unused_loaded_buffer_size_B += buffer_size_B;

	Reference<LoadedBuffer> loaded_buffer = new LoadedBuffer();
	loaded_buffer->buffer = buffer;
	loaded_buffer->buffer_size = buffer_size_B;
	loaded_buffer->total_unused_loaded_buffer_size_B = &resource_manager->total_unused_loaded_buffer_size_B;

	resource->file_size_B = buffer_size_B;

	Reference<ResourceDownloadedMessage> msg = new ResourceDownloadedMessage(downloading_resource->URL, resource);
	msg->loaded_buffer = loaded_buffer;
	out_msg_queue->enqueue(msg); // Send message back to GUIClient
}


void EmscriptenResourceDownloader::onResourceError(Reference<CurrentlyDownloadingResource> downloading_resource)
{
	if(resource_manager.isNull()) // If not initialised:
		return;

	downloading_resources.erase(downloading_resource);
	(*this->num_resources_downloading)--;

	resource_manager->addToDownloadFailedURLs(downloading_resource->URL);

	//ResourceRef resource = resource_manager->getOrCreateResourceForURL(downloading_resource->URL);
	//resource->setState(Resource::State_NotPresent);

	//conPrint("DownloadResourcesThread: Server couldn't send file '" + URL + "'");// (Result=" + toString(result) + ")");
	out_msg_queue->enqueue(new LogMessage("Server couldn't send resource '" + std::string(downloading_resource->URL.begin(), downloading_resource->URL.end()) + "' (resource not found)")); // Send message back to GUIClient
}


void EmscriptenResourceDownloader::think()
{
	const int max_num_concurrent_downloads = 10;
	const int max_total_unused_loaded_buffer_size_B = 256 * 1024 * 1024;

	for(int i=0; i<10; ++i)
	{
		if(resource_manager->total_unused_loaded_buffer_size_B >= max_total_unused_loaded_buffer_size_B)
			conPrint("EmscriptenResourceDownloader: resource_manager->total_unused_loaded_buffer_size_B >= max_total_unused_loaded_buffer_size_B");

		if((downloading_resources.size() < max_num_concurrent_downloads) &&
			(resource_manager->total_unused_loaded_buffer_size_B < max_total_unused_loaded_buffer_size_B))
		{
			DownloadQueueItem item;
			const bool got_item = download_queue->tryDequeueItem(item);
			if(got_item)
			{
				if(!resource_manager->isInDownloadFailedURLs(item.URL)) // Don't try to re-download if we already failed to download this session.
				{
					const URLString URL = item.URL;
					ResourceRef resource = resource_manager->getOrCreateResourceForURL(URL);

					// conPrint("EmscriptenResourceDownloader: considering URL " + URL + "...");

					if(resource->getState() == Resource::State_NotPresent)
					{
						if(gui_client->isDownloadingResourceCurrentlyNeeded(URL))
						{
							Reference<CurrentlyDownloadingResource> downloading_resource = new CurrentlyDownloadingResource();
							downloading_resource->resource_downloader = this;
							downloading_resource->URL = item.URL;
							downloading_resources.insert(downloading_resource);

							(*this->num_resources_downloading)++;

							const URLString http_URL = "/resource/" + URL;
					
							// conPrint("Calling emscripten_async_wget2_data for URL '" + http_URL + "'...");
#if EMSCRIPTEN
							downloading_resource->request_handle = emscripten_async_wget2_data(http_URL.c_str(), /*request type=*/"GET", /*POST params=*/"", 
								/*userdata arg=*/downloading_resource.ptr(), 
								/*free mem=*/0, // we'll free the memory ourselves.
								onLoad, onError, onProgress);
#endif
						}
						//else
						//	conPrint("EmscriptenResourceDownloader: Not downloading resource '" + URL + "' as not currently needed.");
					}
				}
				//else
				//	conPrint("EmscriptenResourceDownloader: resource in failed downlaods.");
			}
		}

	}
}
