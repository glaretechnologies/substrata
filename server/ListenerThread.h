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
struct tls_config;


/*=====================================================================
ListenerThread
--------------

=====================================================================*/
class ListenerThread : public MessageableThread
{
public:
	ListenerThread(int listenport, Server* server, struct ::tls_config* tls_configuration);

	virtual ~ListenerThread();

	virtual void doRun() override;

	virtual void kill() override;

private:
	int listenport;

	Server* server;

	struct tls_config* tls_configuration;

	Reference<MySocket> m_sock;
	glare::AtomicInt should_quit;
};
