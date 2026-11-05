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
#include <AtomicInt.h>
#include <set>
#include <string>
class PrintOutput;
class ThreadMessageSink;
class Server;
class MySocket;
class CurrentTLSServerContextPointer;
struct tls_config;


/*=====================================================================
ListenerThread
--------------

=====================================================================*/
class ListenerThread : public MessageableThread
{
public:
	ListenerThread(int listenport, Server* server, Reference<CurrentTLSServerContextPointer> current_tls_context_ptr);

	virtual ~ListenerThread();

	virtual void doRun() override;

	virtual void kill() override;

private:
	int listenport;

	Server* server;

	Reference<CurrentTLSServerContextPointer> current_tls_context_ptr;

	Reference<MySocket> m_sock;
	glare::AtomicInt should_quit;
};
