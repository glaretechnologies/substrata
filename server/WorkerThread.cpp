/*=====================================================================
WorkerThread.cpp
------------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "WorkerThread.h"


#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <mysocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <ThreadShouldAbortCallback.h>
#include <Parser.h>
#include <MemMappedFile.h>


static const std::string CRLF = "\r\n";
static const bool VERBOSE = false;


WorkerThread::WorkerThread(int thread_id_, const Reference<MySocket>& socket_, SharedRequestHandler* request_handler_)
:	thread_id(thread_id_),
	socket(socket_),
	request_handler(request_handler_)
{
	//if(VERBOSE) print("event_fd.efd: " + toString(event_fd.efd));
}


WorkerThread::~WorkerThread()
{
}


void WorkerThread::doRun()
{
	try
	{
	
	}
	catch(MySocketExcep& e)
	{
		conPrint("Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
	}
}


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("WorkerThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}
