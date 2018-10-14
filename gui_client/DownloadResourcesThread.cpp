/*=====================================================================
DownloadResourcesThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "DownloadResourcesThread.h"


#include "../shared/Protocol.h"
#include "mysocket.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>


DownloadResourcesThread::DownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_, const std::string& hostname_, int port_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	//resources_dir(resources_dir_),
	resource_manager(resource_manager_),
	port(port_)
{}


DownloadResourcesThread::~DownloadResourcesThread()
{}


// Returns true if kill message received
static bool checkMessageQueue(ThreadSafeQueue<Reference<ThreadMessage> >& queue, std::set<std::string>& URLs_to_get)
{
	// Get any more messages from the queue while we're woken up.
	Lock lock(queue.getMutex());
	while(!queue.unlockedEmpty())
	{
		ThreadMessageRef msg;
		queue.unlockedDequeue(msg);

		if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
		{
			URLs_to_get.insert(msg.downcastToPtr<DownloadResourceMessage>()->URL);
		}
		else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
		{
			return true;
		}
	}
	return false;
}


void DownloadResourcesThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("DownloadResourcesThread");

	try
	{
		conPrint("DownloadResourcesThread: Connecting to " + hostname + ":" + toString(port) + "...");

		MySocketRef socket = new MySocket(hostname, port);
		socket->setUseNetworkByteOrder(false);

		conPrint("DownloadResourcesThread: Connected to " + hostname + ":" + toString(port) + "!");

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeDownloadResources); // Write connection type

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw Indigo::Exception("Invalid hello from server: " + toString(hello_response));

		const int MAX_STRING_LEN = 10000;

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw Indigo::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{}
		else
			throw Indigo::Exception("Invalid protocol version response from server: " + toString(protocol_response));

		std::set<std::string> URLs_to_get; // Set of URLs to get from the server

		while(1)
		{
			if(URLs_to_get.empty())
			{
				// Wait on the message queue until we have something to download
				ThreadMessageRef msg;
				getMessageQueue().dequeue(msg);

				if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
				{
					URLs_to_get.insert(msg.downcastToPtr<DownloadResourceMessage>()->URL);
				}
				else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
				{
					return;
				}

				// Get any more messages from the queue while we're woken up.
				if(checkMessageQueue(getMessageQueue(), URLs_to_get))
					return;
			}
			else
			{
				out_msg_queue->enqueue(new ResourceDownloadingStatus(URLs_to_get.size()));

				// Iterate over URLs_to_get, for each one that is not present on disk, add to URLs_to_download and mark as downloading.
				std::vector<std::string> URLs_to_download;
				for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); ++it)
				{
					ResourceRef resource = resource_manager->getResourceForURL(*it);
					if(resource->getState() != Resource::State_NotPresent)
						conPrint("Already have file or downloading file '" + *it + "', not downloading.");
					else
					{
						URLs_to_download.push_back(*it);
						resource->setState(Resource::State_Downloading);
					}
				}

				socket->writeUInt32(Protocol::GetFiles);
				socket->writeUInt64(URLs_to_download.size()); // Write number of files to get

				for(size_t i=0; i<URLs_to_download.size(); ++i)
				{
					conPrint("DownloadResourcesThread: Querying server for file '" + URLs_to_download[i] + "'...");
					socket->writeStringLengthFirst(URLs_to_download[i]);
				}

				// Read reply, which has an error code for each resource download.
				for(size_t i=0; i<URLs_to_download.size(); ++i)
				{
					const std::string& URL = URLs_to_download[i];
					ResourceRef resource = resource_manager->getResourceForURL(URL);

					const uint32 result = socket->readUInt32();
					if(result == 0) // If OK:
					{
						// Download resource
						const uint64 file_len = socket->readUInt64();
						if(file_len > 0)
						{
							// TODO: cap length in a better way
							if(file_len > 1000000000)
								throw Indigo::Exception("downloaded file too large (len=" + toString(file_len) + ").");

							std::vector<uint8> buf(file_len);
							socket->readData(buf.data(), file_len);

							// Save to disk
							const std::string path = resource_manager->pathForURL(URL);
							try
							{
								FileUtils::writeEntireFile(path, (const char*)buf.data(), buf.size());

								conPrint("DownloadResourcesThread: Wrote downloaded file to '" + path + "'. (len=" + toString(file_len) + ") ");

								resource->setState(Resource::State_Present);

								out_msg_queue->enqueue(new ResourceDownloadedMessage(URL));
							}
							catch(FileUtils::FileUtilsExcep& e)
							{
								resource->setState(Resource::State_NotPresent);
								conPrint("DownloadResourcesThread: Error while writing file to disk: " + e.what());
							}
						}

						conPrint("DownloadResourcesThread: Got file.");
					
						URLs_to_get.erase(URL);
						out_msg_queue->enqueue(new ResourceDownloadingStatus(URLs_to_get.size()));
					}
					else
					{
						resource->setState(Resource::State_NotPresent);
						conPrint("DownloadResourcesThread: Server couldn't send file. (Result=" + toString(result) + ")");

						URLs_to_get.erase(URL); // Even though we failed to get this URL, remove from set to get so we don't try and repeatedly download it.
						out_msg_queue->enqueue(new ResourceDownloadingStatus(URLs_to_get.size()));
					}
				}

				if(checkMessageQueue(getMessageQueue(), URLs_to_get))
					return;
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("DownloadResourcesThread Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("DownloadResourcesThread Indigo::Exception: " + e.what());
	}
}
