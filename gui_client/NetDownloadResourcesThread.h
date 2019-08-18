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
#include <IndigoAtomic.h>
#include <mysocket.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;


/*=====================================================================
NetDownloadResourcesThread
--------------------------
Downloads resources from the internet, e.g. via HTTP.
This thread gets sent DownloadResourceMessage from MainWindow, when a new file is needed to be downloaded.
It sends ResourceDownloadedMessage's back to MainWindow via the out_msg_queue when files are downloaded.
=====================================================================*/
class NetDownloadResourcesThread : public MessageableThread
{
public:
	NetDownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<ResourceManager> resource_manager);
	virtual ~NetDownloadResourcesThread();

	virtual void doRun();
	
	virtual void kill();

private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	Reference<ResourceManager> resource_manager;
	IndigoAtomic should_die;
};
