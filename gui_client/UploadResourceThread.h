/*=====================================================================
UploadResourceThread.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#pragma once


#include "../shared/WorldState.h"
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <mysocket.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;



/*=====================================================================
UploadResourceThread
-------------------
Uploads a single file to the server.
=====================================================================*/
class UploadResourceThread : public MessageableThread
{
public:
	UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, const std::string& local_path, const std::string& model_URL, const std::string& hostname, int port);
	virtual ~UploadResourceThread();

	virtual void doRun();

private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	std::string local_path, model_URL;
	std::string hostname;
	int port;
};
