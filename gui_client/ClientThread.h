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
class MainWindow;


class ChatMessage : public ThreadMessage
{
public:
	ChatMessage(const std::string& name_, const std::string& msg_) : name(name_), msg(msg_) {}
	std::string name, msg;
};


// WHen the server wants a file from the client, it will send the client a GetFIle protocol message.  The clientthread will send this 'GetFileMessage' back to MainWindow.
class GetFileMessage : public ThreadMessage
{
public:
	GetFileMessage(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


/*=====================================================================
ClientThread
-------------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MessageableThread
{
public:
	ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, const std::string& hostname, int port, MainWindow* main_window,
		const std::string& username, const std::string& avatar_URL);
	virtual ~ClientThread();

	virtual void doRun();

	void enqueueDataToSend(const std::string& data); // threadsafe

	void killConnection();

	Reference<WorldState> world_state;
	UID client_avatar_uid;
private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	ThreadSafeQueue<std::string> data_to_send;
	EventFD event_fd;
	std::string hostname;
	int port;
	MySocketRef socket;
	MainWindow* main_window;
	std::string username;
	std::string avatar_URL;
};
