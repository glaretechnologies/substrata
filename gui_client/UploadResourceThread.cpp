/*=====================================================================
UploadResourceThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "UploadResourceThread.h"


#include "../shared/Protocol.h"
#include "mysocket.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <PlatformUtils.h>
#include <SocketBufferOutStream.h>


UploadResourceThread::UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& local_path_, const std::string& model_URL_, 
										   const std::string& hostname_, int port_, const std::string& username_, const std::string& password_)
:	//out_msg_queue(out_msg_queue_),
	local_path(local_path_),
	model_URL(model_URL_),
	hostname(hostname_),
	port(port_),
	username(username_),
	password(password_)
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
		socket->setUseNetworkByteOrder(false);

		conPrint("UploadResourceThread: Connected to " + hostname + ":" + toString(port) + "!");

		socket->writeUInt32(CyberspaceHello); // Write hello
		socket->writeUInt32(CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(ConnectionTypeUploadResource); // Write connection type


		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != CyberspaceHello)
			throw Indigo::Exception("Invalid hello from server: " + toString(hello_response));

		const int MAX_STRING_LEN = 10000;

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw Indigo::Exception(msg);
		}
		else if(protocol_response == ClientProtocolOK)
		{
		}
		else
			throw Indigo::Exception("Invalid protocol version response from server: " + toString(protocol_response));


		// We need to log in first, otherwise upload will fail.

		// Make LogInMessage packet and send
		socket->writeUInt32(LogInMessage);
		socket->writeStringLengthFirst(username);
		socket->writeStringLengthFirst(password);

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
