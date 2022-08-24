/*=====================================================================
DownloadResourcesThread.h
-------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "DownloadingResourceQueue.h"
#include "WorldState.h"
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <MySocket.h>
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
	ResourceDownloadedMessage(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


/*=====================================================================
DownloadResourcesThread
-------------------
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
};
