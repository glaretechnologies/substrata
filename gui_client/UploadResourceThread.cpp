/*=====================================================================
UploadResourceThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "UploadResourceThread.h"


#include "mysocket.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <PlatformUtils.h>


UploadResourceThread::UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& local_path_, const std::string& model_URL_, 
										   const std::string& hostname_, int port_)
:	out_msg_queue(out_msg_queue_),
	local_path(local_path_),
	model_URL(model_URL_),
	hostname(hostname_),
	port(port_)
{}


UploadResourceThread::~UploadResourceThread()
{}


void UploadResourceThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("UploadResourceThread");

	try
	{
		conPrint("UploadResourceThread: Connecting to " + hostname + ":" + toString(port) + "...");

		MySocketRef socket = new MySocket(hostname, port);

		conPrint("UploadResourceThread: Connected to " + hostname + ":" + toString(port) + "!");

		// Write connection type
		socket->writeUInt32(ConnectionTypeUploadResource);

		// Load resource
		MemMappedFile file(local_path);

		socket->writeUInt32(UploadResource);

		socket->writeStringLengthFirst(model_URL); // Write URL

		socket->writeUInt64(file.fileSize()); // Write file size

		socket->writeData(file.fileData(), file.fileSize());

		conPrint("UploadResourceThread: Sent file. (" + toString(file.fileSize()) + " B)");
	}
	catch(MySocketExcep& e)
	{
		conPrint("UploadResourceThread Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("UploadResourceThread Indigo::Exception: " + e.what());
	}
}
