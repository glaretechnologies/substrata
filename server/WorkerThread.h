/*=====================================================================
WorkerThread.h
--------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <mysocket.h>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;


/*=====================================================================
WorkerThread
------------
This thread runs on the server, and handles communication with a single client.
=====================================================================*/
class WorkerThread : public MessageableThread
{
public:
	// May throw Indigo::Exception from constructor if EventFD init fails.
	WorkerThread(int thread_id, const Reference<MySocket>& socket, Server* server);
	virtual ~WorkerThread();

	virtual void doRun();


	void enqueueDataToSend(const std::string& data); // threadsafe

private:
	void sendGetFileMessageIfNeeded(const std::string& resource_URL);

	ThreadSafeQueue<std::string> data_to_send;

	Reference<MySocket> socket;
	Server* server;
	EventFD event_fd;	
};
