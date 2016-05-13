/*=====================================================================
ClientThread.h
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
ClientThread
-------------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MyThread
{
public:
	ClientThread();
	virtual ~ClientThread();

	virtual void run();

	void enqueueDataToSend(const std::string& data); // threadsafe

	Reference<WorldState> world_state;
	UID client_avatar_uid;
private:
	ThreadSafeQueue<std::string> data_to_send;
	EventFD event_fd;
};
