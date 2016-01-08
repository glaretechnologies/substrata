/*=====================================================================
WorkerThread.h
----------------
=====================================================================*/
#pragma once


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
class SharedRequestHandler;


/*=====================================================================
WorkerThread
------------

=====================================================================*/
class WorkerThread : public MessageableThread
{
public:
	// May throw Indigo::Exception from constructor if EventFD init fails.
	WorkerThread(int thread_id, const Reference<MySocket>& socket, SharedRequestHandler* request_handler);
	virtual ~WorkerThread();

	virtual void doRun();


	void enqueueDataToSend(const std::string& data); // threadsafe

private:
	ThreadSafeQueue<std::string> data_to_send;

	int thread_id;
	Reference<MySocket> socket;
	SharedRequestHandler* request_handler;
	EventFD event_fd;
};
