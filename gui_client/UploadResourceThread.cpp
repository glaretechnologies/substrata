/*=====================================================================
UploadResourceThread.cpp
------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "UploadResourceThread.h"


#include "../shared/Protocol.h"
#include <MySocket.h>
#include <TLSSocket.h>
#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <PlatformUtils.h>
#if EMSCRIPTEN
#include <networking/EmscriptenWebSocket.h>
#endif


UploadResourceThread::UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, ThreadSafeQueue<Reference<ResourceToUpload>>* upload_queue_,
										const std::string& hostname_, int port_, const std::string& username_, const std::string& password_, struct tls_config* config_,
										glare::AtomicInt* num_resources_uploading_)
:	upload_queue(upload_queue_),
	hostname(hostname_),
	port(port_),
	username(username_),
	password(password_),
	config(config_),
	num_resources_uploading(num_resources_uploading_)
{}


UploadResourceThread::~UploadResourceThread()
{}


void UploadResourceThread::kill()
{
	should_die = glare::atomic_int(1);
}


// This executes in the DownloadResourcesThread context.
// We call ungracefulShutdown() on the socket.  This results in any current blocking call returning with WSAEINTR ('blocking operation was interrupted by a call to WSACancelBlockingCall')
#if defined(_WIN32)
static void asyncProcedure(uint64 data)
{
	UploadResourceThread* upload_thread = (UploadResourceThread*)data;
	SocketInterfaceRef socket = upload_thread->socket;
	if(socket.nonNull())
		socket->ungracefulShutdown();

	upload_thread->decRefCount();
}
#endif


void UploadResourceThread::killConnection()
{
#if defined(_WIN32)
	this->incRefCount();
	QueueUserAPC(asyncProcedure, this->getHandle(), /*data=*/(ULONG_PTR)this);
#else
	if(socket.nonNull())
		socket->ungracefulShutdown();
#endif
}


void UploadResourceThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("UploadResourceThread");

	while(!should_die)
	{
		Reference<ResourceToUpload> resource_to_upload;
		if(upload_queue->dequeueWithTimeout(/*wait_time_s=*/0.4, resource_to_upload)) // If got a ResourceToUpload:
		{
			const std::string local_path   = resource_to_upload->local_path;
			const URLString resource_URL = resource_to_upload->resource_URL;

			try
			{
				conPrint("UploadResourceThread: Connecting to " + hostname + ":" + toString(port) + "...");

				socket = nullptr;
#if EMSCRIPTEN
				socket = new EmscriptenWebSocket();

				const bool use_TLS = hostname != "localhost"; // Don't use TLS on localhost for now, for testing.
				const std::string protocol = use_TLS ? "wss" : "ws";
				socket.downcast<EmscriptenWebSocket>()->connect(protocol, hostname, /*port=*/use_TLS ? 443 : 80);
#else

				MySocketRef plain_socket = new MySocket(hostname, port);
				plain_socket->setUseNetworkByteOrder(false);

				socket = new TLSSocket(plain_socket, config, hostname);
#endif

				conPrint("UploadResourceThread: Connected to " + hostname + ":" + toString(port) + "!");

				socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
				socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
				socket->writeUInt32(Protocol::ConnectionTypeUploadResource); // Write connection type
				socket->flush();

				conPrint("UploadResourceThread: reading hello...");
				// Read hello response from server
				const uint32 hello_response = socket->readUInt32();
				if(hello_response != Protocol::CyberspaceHello)
					throw glare::Exception("Invalid hello from server: " + toString(hello_response));

				conPrint("UploadResourceThread: read hello_response.");

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

				// Read server_mesh_optimisation_version
				[[maybe_unused]] int server_mesh_optimisation_version = 1;
				if(server_protocol_version >= 43)
					server_mesh_optimisation_version = socket->readInt32();

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
					// TEMP HACK IMPORTANT: write half the file then quit
					//socket->writeData(file.fileData(), file.fileSize() / 2);
					//throw glare::Exception("TEST QUITTING UPLOAD");
					socket->writeData(file.fileData(), file.fileSize());
					conPrint("UploadResourceThread: Sent file '" + local_path + "', URL '" + toStdString(resource_URL) + "' (" + toString(file.fileSize()) + " B)");
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

			socket = nullptr;

			(*num_resources_uploading)--;
		}
	} // end while(!should_die)
}
