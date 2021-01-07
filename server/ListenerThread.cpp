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
#include <ThreadShouldAbortCallback.h>
#include <Exception.h>



ListenerThread::ListenerThread(int listenport_, Server* server_)
:	listenport(listenport_), server(server_)
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
			conPrint("Successfully bound and listening on port " + toString(listenport));
		else
			throw MySocketExcep("Failed to bind and listen.");

		int next_thread_id = 0;
		while(1)
		{
			MySocketRef workersock = sock->acceptConnection(); // Blocks
			workersock->setUseNetworkByteOrder(false);

			conPrint("Client connected from " + IPAddress::formatIPAddressAndPort(workersock->getOtherEndIPAddress(), workersock->getOtherEndPort()));

			Reference<WorkerThread> worker_thread = new WorkerThread(
				next_thread_id,
				workersock,
				server
			);

			next_thread_id++;
			
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
	catch(Indigo::Exception& e)
	{
		conPrint("ListenerThread Indigo::Exception: " + e.what());
	}
	

	// Kill the child WorkerThread threads now
	//thread_manager.killThreadsBlocking();

	conPrint("ListenerThread terminated.");
}
