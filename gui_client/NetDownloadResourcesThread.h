/*=====================================================================
NetDownloadResourcesThread.h
----------------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#pragma once


#include "DownloadResourcesThread.h"
#include "../shared/ResourceManager.h"
#include "WorldState.h"
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <AtomicInt.h>
#include <MySocket.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;
class HTTPClient;


class DownloadResourceMessage : public ThreadMessage
{
public:
	DownloadResourceMessage(const URLString& URL_) : URL(URL_) {}
	URLString URL;

	glare::AtomicInt processed; // zero if not processed (being downloaded) yet.
};


/*=====================================================================
NetDownloadResourcesThread
--------------------------
Downloads resources from the internet, e.g. via HTTP.
This thread gets sent DownloadResourceMessage from MainWindow, when a new file is needed to be downloaded.
It sends ResourceDownloadedMessages back to MainWindow via the out_msg_queue when files are downloaded.
=====================================================================*/
class NetDownloadResourcesThread : public MessageableThread
{
public:
	NetDownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<ResourceManager> resource_manager,
		glare::AtomicInt* num_net_resources_downloading_);
	virtual ~NetDownloadResourcesThread();

	virtual void doRun();
	
	virtual void kill();

private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	Reference<ResourceManager> resource_manager;
	glare::AtomicInt* num_net_resources_downloading;
	glare::AtomicInt should_die;
	Reference<HTTPClient> client;
};
