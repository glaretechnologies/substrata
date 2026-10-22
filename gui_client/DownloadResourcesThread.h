/*=====================================================================
DownloadResourcesThread.h
-------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/URLString.h"
#include "ThreadMessages.h"
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
	ResourceDownloadedMessage(const URLString& URL_, const ResourceRef& resource_) : ThreadMessage(Msg_ResourceDownloadedMessage), URL(URL_), resource(resource_) {}
	URLString URL;
	ResourceRef resource;

	Reference<LoadedBuffer> loaded_buffer; // For emscripten, where we will load directly into memory instead of to disk.
};


// The server couldn't send the resource, so it has been added to the resource manager's download-failed URLs.
class ResourceDownloadFailedMessage : public ThreadMessage
{
public:
	enum Reason
	{
		Reason_NotFound,
		Reason_BeingProcessed // The server is generating the resource, e.g. a LOD or basis version of a texture.  A NewResourceOnServerMessage follows when it is done.
	};

	ResourceDownloadFailedMessage(const URLString& URL_, Reason reason_) : ThreadMessage(Msg_ResourceDownloadFailedMessage), URL(URL_), reason(reason_) {}
	URLString URL;
	Reason reason;
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
