/*=====================================================================
UploadResourceThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "UploadResourceThread.h"


#include "../shared/Protocol.h"
#include <MySocket.h>
#include <TLSSocket.h>
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <PlatformUtils.h>
#include <SocketBufferOutStream.h>


UploadResourceThread::UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& local_path_, const std::string& resource_URL_, 
										const std::string& hostname_, int port_, const std::string& username_, const std::string& password_, struct tls_config* config_,
										glare::AtomicInt* num_resources_uploading_)
:	//out_msg_queue(out_msg_queue_),
	local_path(local_path_),
	resource_URL(resource_URL_),
	hostname(hostname_),
	port(port_),
	username(username_),
	password(password_),
	config(config_),
	num_resources_uploading(num_resources_uploading_)
{}


UploadResourceThread::~UploadResourceThread()
{}


void UploadResourceThread::doRun()
{
#if !defined(EMSCRIPTEN)
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("UploadResourceThread");

	try
	{
		//conPrint("UploadResourceThread: Connecting to " + hostname + ":" + toString(port) + "...");

		MySocketRef plain_socket = new MySocket(hostname, port);
		plain_socket->setUseNetworkByteOrder(false);

		TLSSocketRef socket = new TLSSocket(plain_socket, config, hostname);

		//conPrint("UploadResourceThread: Connected to " + hostname + ":" + toString(port) + "!");

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeUploadResource); // Write connection type

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw glare::Exception("Invalid hello from server: " + toString(hello_response));

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(10000);
			throw glare::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{
		}
		else
			throw glare::Exception("Invalid protocol version response from server: " + toString(protocol_response));

		// Read server protocol version
		const uint32 server_protocol_version = socket->readUInt32();

		// Read server capabilities
		[[maybe_unused]] uint32 server_capabilities = 0;
		if(server_protocol_version >= 41)
			server_capabilities = socket->readUInt32();

		// Send login details
		socket->writeStringLengthFirst(username);
		socket->writeStringLengthFirst(password);


		// Load resource from disk
		MemMappedFile file(local_path);

		socket->writeStringLengthFirst(resource_URL); // Write URL

		socket->writeUInt64(file.fileSize()); // Write file size

		// Read back response from server first, to avoid sending all the data if the upload is not allowed.
		const uint32 response = socket->readUInt32();
		if(response == Protocol::UploadAllowed)
		{
			socket->writeData(file.fileData(), file.fileSize());
			conPrint("UploadResourceThread: Sent file '" + local_path + "', URL '" + resource_URL + "' (" + toString(file.fileSize()) + " B)");
		}
		else
		{
			const std::string msg = socket->readStringLengthFirst(1000);
			conPrint("UploadResourceThread: received error code " + toString(response) + " while trying to upload resource: '" + msg + "'");
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("UploadResourceThread Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("UploadResourceThread glare::Exception: " + e.what());
	}

	(*num_resources_uploading)--;
#endif
}
