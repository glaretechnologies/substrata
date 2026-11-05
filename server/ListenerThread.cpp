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
#include <OpenSSL.h>


ListenerThread::ListenerThread(int listenport_, Server* server_, Reference<CurrentTLSServerContextPointer> current_tls_context_ptr_)
:	listenport(listenport_), server(server_), current_tls_context_ptr(current_tls_context_ptr_)
{
}


ListenerThread::~ListenerThread()
{
}


void ListenerThread::doRun()
{
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
			conPrint("ListenerThread: Successfully bound and listening on port " + toString(listenport));
		else
			throw MySocketExcep("Failed to bind and listen.");

		m_sock = sock;

		while(!should_quit)
		{
			try
			{
				MySocketRef plain_worker_sock = sock->acceptConnection(); // Blocks
				plain_worker_sock->setUseNetworkByteOrder(false);

				conPrint("Client connected from " + IPAddress::formatIPAddressAndPort(plain_worker_sock->getOtherEndIPAddress(), plain_worker_sock->getOtherEndPort()));

				plain_worker_sock->enableTCPKeepAlive(30.f); // Some connections seem to get stuck doing nothing for long periods, so enable keepalive to kill them.

				// Create TLSContext and TLSSocket for worker thread/socket if this is configured as a TLS connection.
				SocketInterfaceRef use_socket = plain_worker_sock;
				if(current_tls_context_ptr)
				{
					// Get the current TLSContext that should be used by the listener.  This may change occasionally when the TLS configuration changes (new keys etc.)
					TLSContextRef cur_listener_context = current_tls_context_ptr->getCurrentContext();

					TLSContextRef worker_tls_context = new TLSContext();
					if(tls_accept_socket(cur_listener_context->context, /*new context out=*/&worker_tls_context->context, (int)plain_worker_sock->getSocketHandle()) != 0)
						throw glare::Exception("tls_accept_socket failed: " + getTLSErrorString(cur_listener_context));

					worker_tls_context->server_context = cur_listener_context;

					TLSSocketRef worker_tls_socket = new TLSSocket(plain_worker_sock, worker_tls_context);
					use_socket = worker_tls_socket; // use_socket will be a TLS socket after this.
				}

				// Handle the connection in a worker thread.
				Reference<WorkerThread> worker_thread = new WorkerThread(
					use_socket,
					server,
					false // is_websocket_connection
				);

				server->worker_thread_manager.addThread(worker_thread);
			}
			catch(glare::Exception& e)
			{
				conPrint("ListenerThread: caught exception: " + e.what());
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

	current_tls_context_ptr = nullptr;

	OpenSSL::threadCleanup();

	conPrint("ListenerThread terminating.");
}


void ListenerThread::kill()
{
	should_quit = 1;

	MySocketRef sock = this->m_sock;
	if(sock)
		sock->ungracefulShutdown();
}
