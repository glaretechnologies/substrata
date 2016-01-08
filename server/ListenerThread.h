/*=====================================================================
listenerthread.h
----------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <ThreadManager.h>
#include <set>
#include <string>
class PrintOutput;
class ThreadMessageSink;
class DataStore;
class SharedRequestHandler;


/*=====================================================================
ListenerThread
--------------

=====================================================================*/
class ListenerThread : public MessageableThread
{
public:
	ListenerThread(int listenport, SharedRequestHandler* shared_request_handler);

	virtual ~ListenerThread();

	virtual void doRun();

private:
	int listenport;

	// Child threads are
	// * WorkerThread's
	ThreadManager thread_manager;

	SharedRequestHandler* shared_request_handler;
};
