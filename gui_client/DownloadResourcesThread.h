/*=====================================================================
DownloadResourcesThread.h
-------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/URLString.h"
#include "DownloadingResourceQueue.h"
#include "WorldState.h"
#include <MySocket.h>
#include <utils/MessageableThread.h>
#include <utils/Platform.h>
#include <utils/MyThread.h>
#include <utils/EventFD.h>
#include <utils/ThreadManager.h>
#include <utils/ThreadSafeQueue.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;
namespace glare { class AtomicInt; }
struct tls_config;
class DownloadingResourceQueue;


class ResourceDownloadedMessage : public ThreadMessage
{
public:
	ResourceDownloadedMessage(const URLString& URL_, const ResourceRef& resource_) : URL(URL_), resource(resource_) {}
	URLString URL;
	ResourceRef resource;

	Reference<LoadedBuffer> loaded_buffer; // For emscripten, where we will load directly into memory instead of to disk.
};


/*=====================================================================
DownloadResourcesThread
-----------------------
Downloads any resources from the server as needed.
This thread gets sent DownloadResourceMessage from MainWindow, when a new file is needed to be downloaded.
It sends ResourceDownloadedMessages back to MainWindow via the out_msg_queue when files are downloaded.
=====================================================================*/
class DownloadResourcesThread : public MessageableThread
{
public:
	DownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<ResourceManager> resource_manager, const std::string& hostname, int port,
		glare::AtomicInt* num_resources_downloading_, struct tls_config* config, DownloadingResourceQueue* download_queue_);
	virtual ~DownloadResourcesThread();

	virtual void doRun();

	virtual void kill();

	void killConnection();

private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	Reference<ResourceManager> resource_manager;
	std::string hostname;
	int port;
	glare::AtomicInt* num_resources_downloading;
	struct tls_config* config;

	DownloadingResourceQueue* download_queue;

	std::vector<DownloadQueueItem> queue_items; // scratch buffer

	glare::AtomicInt should_die;
public:
	SocketInterfaceRef socket;
};
