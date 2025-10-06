/*=====================================================================
DownloadResourcesThread.cpp
---------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "DownloadResourcesThread.h"


#include "DownloadingResourceQueue.h"
#include "ThreadMessages.h"
#include "../shared/Protocol.h"
#include <MySocket.h>
#include <TLSSocket.h>
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>
#include <FileOutStream.h>


DownloadResourcesThread::DownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_, const std::string& hostname_, int port_, 
	glare::AtomicInt* num_resources_downloading_, struct tls_config* config_, DownloadingResourceQueue* download_queue_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	resource_manager(resource_manager_),
	port(port_),
	num_resources_downloading(num_resources_downloading_),
	config(config_),
	download_queue(download_queue_)
{
	MySocketRef mysocket = new MySocket();
	mysocket->setUseNetworkByteOrder(false);
	socket = mysocket;
}


DownloadResourcesThread::~DownloadResourcesThread()
{}


// Returns true if kill message received
static bool checkMessageQueue(ThreadSafeQueue<Reference<ThreadMessage> >& queue)
{
	// Get any more messages from the queue while we're woken up.
	Lock lock(queue.getMutex());
	while(!queue.unlockedEmpty())
	{
		ThreadMessageRef msg;
		queue.unlockedDequeue(msg);

		if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
			return true;
	}
	return false;
}


// Make sure num_resources_downloading gets decremented even in the presence of exceptions.
struct NumNonNetResourcesDownloadingDecrementor
{
	NumNonNetResourcesDownloadingDecrementor() : num_decrements(0) {}
	~NumNonNetResourcesDownloadingDecrementor()
	{
		// Do any remaining decrements
		(*num_resources_downloading) -= (max_decrements - num_decrements);
	}
	glare::AtomicInt* num_resources_downloading;
	int max_decrements;
	int num_decrements;
};

// Some resources, such as MP4 videos, shouldn't be downloaded fully before displaying, but instead can be streamed and displayed when only part of the stream is downloaded.
//static bool shouldStreamResource(const std::string& url)
//{
//	return ::hasExtension(url, "mp4");
//}


void DownloadResourcesThread::kill()
{
	should_die = glare::atomic_int(1);
}


// This executes in the DownloadResourcesThread context.
// We call ungracefulShutdown() on the socket.  This results in any current blocking call returning with WSAEINTR ('blocking operation was interrupted by a call to WSACancelBlockingCall')
#if defined(_WIN32)
static void asyncProcedure(uint64 data)
{
	DownloadResourcesThread* download_thread = (DownloadResourcesThread*)data;
	if(download_thread->socket.nonNull())
		download_thread->socket->ungracefulShutdown();

	download_thread->decRefCount();
}
#endif


void DownloadResourcesThread::killConnection()
{
#if defined(_WIN32)
	this->incRefCount();
	QueueUserAPC(asyncProcedure, this->getHandle(), /*data=*/(ULONG_PTR)this);
#else
	if(socket.nonNull())
		socket->ungracefulShutdown();
#endif
}


void DownloadResourcesThread::doRun()
{
#if !EMSCRIPTEN // Emscripten uses EmscriptenResourceDownloader instead.

	PlatformUtils::setCurrentThreadNameIfTestsEnabled("DownloadResourcesThread");

	try
	{
		// conPrint("DownloadResourcesThread: Connecting to " + hostname + ":" + toString(port) + "...");

		socket.downcast<MySocket>()->connect(hostname, port);

		socket = new TLSSocket(socket.downcast<MySocket>(), config, hostname);

		// conPrint("DownloadResourcesThread: Connected to " + hostname + ":" + toString(port) + "!");

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeDownloadResources); // Write connection type

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw glare::Exception("Invalid hello from server: " + toString(hello_response));

		const int MAX_STRING_LEN = 10000;

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw glare::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{}
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

		std::set<URLString> URLs_to_get; // Set of URLs that this thread will get from the server.

		while(1)
		{
			if(should_die)
			{
				socket->writeInt32(Protocol::CyberspaceGoodbye);
				socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the server.
				return;
			}

			if(URLs_to_get.empty())
			{
				// Wait until we have something to download, or we get a kill-thread message.

				download_queue->dequeueItemsWithTimeOut(/*wait_time_s=*/0.1, /*max_num_items=*/4, queue_items);
				for(size_t i=0; i<queue_items.size(); ++i)
				{
					if(!resource_manager->isInDownloadFailedURLs(queue_items[i].URL)) // Don't try to re-download if we already failed to download this session.
						URLs_to_get.insert(queue_items[i].URL);
				}


				// Get any more messages from the queue while we're woken up.
				if(checkMessageQueue(getMessageQueue()))
				{
					socket->writeInt32(Protocol::CyberspaceGoodbye);
					socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the server.
					return; // if got kill message, return.
				}
			}
			else
			{
				for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); )
				{
					ResourceRef resource = resource_manager->getOrCreateResourceForURL(*it);
					if(resource->getState() != Resource::State_NotPresent)
					{
						//conPrint("Already have file or downloading file '" + *it + "', not downloading.");
						auto to_remove = it++;
						URLs_to_get.erase(to_remove);
					}
					else
					{
						resource->setState(Resource::State_Transferring);
						++it;
					}
				}

				(*this->num_resources_downloading) += URLs_to_get.size();
				NumNonNetResourcesDownloadingDecrementor decrementor;
				decrementor.num_resources_downloading = this->num_resources_downloading;
				decrementor.max_decrements = (int)URLs_to_get.size();

				if(!URLs_to_get.empty())
				{
					socket->writeUInt32(Protocol::GetFiles);
					socket->writeUInt64(URLs_to_get.size()); // Write number of files to get

					for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); ++it)
					{
						const URLString URL = *it;

						// conPrint("DownloadResourcesThread: Querying server for file '" + URL + "'...");
						socket->writeStringLengthFirst(URL);
					}

					// Read reply, which has an error code for each resource download.
					for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); ++it)
					{
						const URLString URL = *it;
						ResourceRef resource = resource_manager->getOrCreateResourceForURL(URL);

						
						const uint32 result = socket->readUInt32();
						if(result == 0) // If OK:
						{
							// Download resource
							const uint64 file_len = socket->readUInt64();
							if(file_len > 0)
							{
								if(file_len > 1000000000)
									throw glare::Exception("downloaded file too large (len=" + toString(file_len) + ").");

								resource->setState(Resource::State_Transferring);

								try
								{
									const std::string path = resource_manager->getLocalAbsPathForResource(*resource);
									{
										FileOutStream file(path, std::ios::binary | std::ios::trunc); // Remove any existing data in the file

										uint64 offset = 0;
										const uint64 MAX_CHUNK_SIZE = 1ull << 14;
										js::Vector<uint8, 16> temp_buf(MAX_CHUNK_SIZE);
										while(offset < file_len)
										{
											const uint64 chunk_size = myMin(file_len - offset, MAX_CHUNK_SIZE);
											assert(offset + chunk_size <= file_len);
											socket->readData(temp_buf.data(), chunk_size);
											file.writeData(temp_buf.data(), chunk_size);
											offset += chunk_size;

											if(should_die)
												throw glare::Exception("Interrupted");
										}

										file.close(); // Manually call close, to check for any errors via failbit.
									} // End scope for FileOutStream

									resource->setState(Resource::State_Present);
									resource_manager->markAsChanged();

									out_msg_queue->enqueue(new ResourceDownloadedMessage(URL, resource));
								}
								catch(glare::Exception& e)
								{
									resource->setState(Resource::State_NotPresent);
									resource_manager->markAsChanged();

									//conPrint("DownloadResourcesThread: Error while writing file to disk: " + e.what());
									out_msg_queue->enqueue(new LogMessage("DownloadResourcesThread: Error while writing file to disk: " + e.what()));
								}
							}

							//conPrint("DownloadResourcesThread: Got file '" + URL + "'.");
						}
						else
						{
							resource_manager->addToDownloadFailedURLs(URL);

							resource->setState(Resource::State_NotPresent);
							//conPrint("DownloadResourcesThread: Server couldn't send file '" + URL + "' (Result=" + toString(result) + ")");
							out_msg_queue->enqueue(new LogMessage("Server couldn't send resource '" + std::string(URL.begin(), URL.end()) + "' (resource not found)"));
						}

						(*this->num_resources_downloading)--;
						decrementor.num_decrements++;
					} // End for each URL
				}

				URLs_to_get.clear();
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("DownloadResourcesThread Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("DownloadResourcesThread glare::Exception: " + e.what());
	}
#endif
}












#if 0
//const bool stream = shouldStreamResource(URL);
// 
// If 'streaming' the file, download in chunks, while locking the buffer mutex.
// This is so the main thread can read from the resource buffer.

if(stream)
{
	{
		Lock lock(resource->buffer_mutex);
		resource->buffer.reserve(file_len);
	}

	js::Vector<uint8, 16> temp_buf(1024 * 16);

	size_t buf_i = 0;
	size_t readlen = file_len;
	while(readlen > 0) // While still bytes to read
	{
		const size_t num_bytes_to_read = myMin<size_t>(temp_buf.size(), readlen);
		assert(num_bytes_to_read > 0);

		socket->readData(temp_buf.data(), num_bytes_to_read);

		{
			Lock lock(resource->buffer_mutex);
			resource->buffer.resize(buf_i + num_bytes_to_read);
			std::memcpy(&resource->buffer[buf_i], temp_buf.data(), num_bytes_to_read);

			//for(auto list_it = resource->listeners.begin(); list_it != resource->listeners.end(); ++list_it)
			//	(*list_it)->dataReceived();
		}

		readlen -= num_bytes_to_read;
		buf_i += num_bytes_to_read;
	}
}
else
{
	Lock lock(resource->buffer_mutex);
	resource->buffer.resize(file_len);
	socket->readData(resource->buffer.data(), file_len); // Just read entire file.
}
#endif

#if 0
// Write downloaded file to disk, clear in-mem buffer.
FileUtils::writeEntireFile(path, (const char*)resource->buffer.data(), resource->buffer.size());

{
	Lock lock(resource->buffer_mutex);
	if(resource->num_buffer_readers == 0) // Only clear if no other threads reading buffer.
		resource->buffer.clearAndFreeMem(); // TODO: clear resource buffer later when num readers drops to zero.
}
#endif