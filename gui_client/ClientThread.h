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


class ChatMessage : public ThreadMessage
{
public:
	ChatMessage(const std::string& name_, const std::string& msg_) : name(name_), msg(msg_) {}
	virtual ThreadMessage* clone() const { return new ChatMessage(name, msg); }
	std::string name, msg;
};


/*=====================================================================
ClientThread
-------------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MyThread
{
public:
	ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue);
	virtual ~ClientThread();

	virtual void run();

	void enqueueDataToSend(const std::string& data); // threadsafe

	Reference<WorldState> world_state;
	UID client_avatar_uid;
private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	ThreadSafeQueue<std::string> data_to_send;
	EventFD event_fd;
};
