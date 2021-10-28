/*=====================================================================
listenerthread.cpp
------------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "ListenerThread.h"


#include "Server.h"
#include "WorkerThread.h"
#include <ConPrint.h>
#include <MySocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Exception.h>
#include <Timer.h>
#include <tls.h>
#include <TLSSocket.h>


ListenerThread::ListenerThread(int listenport_, Server* server_, struct tls_config* tls_configuration_)
:	listenport(listenport_), server(server_), tls_configuration(tls_configuration_)
{
}


ListenerThread::~ListenerThread()
{
}


void ListenerThread::doRun()
{
	struct tls* tls_context = NULL;
	try
	{
		MySocketRef sock;

		const int MAX_NUM_ATTEMPTS = 600;
		bool bound = false;
		for(int i=0; i<MAX_NUM_ATTEMPTS; ++i)
		{
			// Create new socket
			sock = new MySocket();

			try
			{
				sock->bindAndListen(listenport, /*reuse address=*/true);
				bound = true;
				break;
			}
			catch(MySocketExcep& e)
			{
				conPrint("bindAndListen failed: " + e.what() + ", waiting and retrying...");
				PlatformUtils::Sleep(5000);
			}
		}

		if(bound)
			conPrint("Successfully bound and listening on port " + toString(listenport));
		else
			throw MySocketExcep("Failed to bind and listen.");

		if(tls_configuration)
		{
			tls_context = tls_server();
			if(!tls_context)
				throw glare::Exception("Failed to create tls_context.");
			if(tls_configure(tls_context, tls_configuration) == -1)
				throw glare::Exception("tls_configure failed: " + getTLSErrorString(tls_context));
		}

		while(1)
		{
			MySocketRef plain_worker_sock = sock->acceptConnection(); // Blocks
			plain_worker_sock->setUseNetworkByteOrder(false);

			conPrint("Client connected from " + IPAddress::formatIPAddressAndPort(plain_worker_sock->getOtherEndIPAddress(), plain_worker_sock->getOtherEndPort()));

			// Create TLSSocket (tls_context) for worker thread/socket if this is configured as a TLS connection.
			SocketInterfaceRef use_socket = plain_worker_sock;
			if(tls_context)
			{
				struct tls* worker_tls_context = NULL;
				if(tls_accept_socket(tls_context, &worker_tls_context, (int)plain_worker_sock->getSocketHandle()) != 0)
					throw glare::Exception("tls_accept_socket failed: " + getTLSErrorString(tls_context));

				TLSSocketRef worker_tls_socket = new TLSSocket(plain_worker_sock, worker_tls_context);
				use_socket = worker_tls_socket; // use_socket will be a TLS socket after this.
			}
			
			// Handle the connection in a worker thread.
			Reference<WorkerThread> worker_thread = new WorkerThread(
				use_socket,
				server
			);

			try
			{
				//thread_manager.addThread(worker_thread);
				server->worker_thread_manager.addThread(worker_thread);
			}
			catch(MyThreadExcep& e)
			{
				// Will get this when thread creation fails.
				conPrint("ListenerThread failed to launch worker thread: " + e.what());
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("ListenerThread: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("ListenerThread glare::Exception: " + e.what());
	}

	if(tls_context != NULL)
		tls_free(tls_context); // Free TLS context.

	

	// Kill the child WorkerThread threads now
	//thread_manager.killThreadsBlocking();

	conPrint("ListenerThread terminated.");
}
