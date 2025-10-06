/*=====================================================================
NetDownloadResourcesThread.cpp
------------------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "NetDownloadResourcesThread.h"


#include "../shared/Protocol.h"
#include "HTTPClient.h"
#include "URL.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>


NetDownloadResourcesThread::NetDownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_,
	glare::AtomicInt* num_net_resources_downloading_)
:	out_msg_queue(out_msg_queue_),
	resource_manager(resource_manager_),
	num_net_resources_downloading(num_net_resources_downloading_)
{
#if !defined(EMSCRIPTEN)
	client = new HTTPClient();
	client->additional_headers.push_back("User-Agent: Substrata client");
#endif
}


NetDownloadResourcesThread::~NetDownloadResourcesThread()
{
}


void NetDownloadResourcesThread::kill()
{
#if !defined(EMSCRIPTEN)
	should_die = 1;
	client->kill();
#endif
}


// Make sure num_net_resources_downloading gets decremented even in the presence of exceptions.
struct NumResourcesDownloadingDecrementor
{
	~NumResourcesDownloadingDecrementor() { (*num_net_resources_downloading)--; }
	glare::AtomicInt* num_net_resources_downloading;
};


static const bool VERBOSE = false;


void NetDownloadResourcesThread::doRun()
{
#if !defined(EMSCRIPTEN)
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("NetDownloadResourcesThread");

	try
	{
		
		std::set<URLString> URLs_to_get; // Set of URLs to get from the server

		while(1)
		{
			if(URLs_to_get.empty())
			{
				// Wait on the message queue until we have something to download
				ThreadMessageRef msg;
				getMessageQueue().dequeue(msg);

				if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
				{
					if(msg.downcastToPtr<DownloadResourceMessage>()->processed.increment() == 0) // If this is the first thread to process this message:
						URLs_to_get.insert(msg.downcastToPtr<DownloadResourceMessage>()->URL);
				}
				else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
				{
					return;
				}
			}
			else
			{
				if(this->should_die != 0)
					return;

				NumResourcesDownloadingDecrementor d;
				d.num_net_resources_downloading = this->num_net_resources_downloading;

				URLString url = *URLs_to_get.begin();
				URLs_to_get.erase(URLs_to_get.begin());

				ResourceRef resource = resource_manager->getOrCreateResourceForURL(url);

				// Check to see if we have the resource now, we may have downloaded it recently.
				if(resource->getState() != Resource::State_NotPresent)
				{
					//conPrint("Already have file, not downloading.");
				}
				else
				{
					if(VERBOSE) conPrint("NetDownloadResourcesThread: Downloading file '" + toStdString(url) + "'...");

					resource->setState(Resource::State_Transferring);

					try
					{
						std::vector<uint8> data;

						// Parse URL
						const URL url_components = URL::parseURL(toStdString(url));
						if(url_components.scheme == "http" || url_components.scheme == "https")
						{
							if(url_components.host == "gateway.ipfs.io")
								throw glare::Exception("Skipping " + toStdString(url));

							// Download with HTTP client
							client->max_data_size			= 128 * 1024 * 1024; // 128 MB
							client->max_socket_buffer_size	= 128 * 1024 * 1024; // 128 MB
							HTTPClient::ResponseInfo response_info = client->downloadFile(toStdString(url), data);
							if(response_info.response_code != 200)
								throw glare::Exception("HTTP Download failed: (code: " + toString(response_info.response_code) + "): " + response_info.response_message);

							std::string extension;
							if(response_info.mime_type == "image/bmp")
								extension = "bmp";
							else if(response_info.mime_type == "image/jpeg")
								extension = "jpg";
							else if(response_info.mime_type == "image/png")
								extension = "png";
							else if(response_info.mime_type == "image/gif")
								extension = "gif";
							else if(response_info.mime_type == "image/tiff")
								extension = "tif";

							// Add an extension based on the mime type.
							if(!extension.empty())
								if(!hasExtension(resource->getRawLocalPath(), extension))
								{
									resource->setRawLocalPath(resource->getRawLocalPath() + "." + extension);

									// Avoid path being too long for Windows now that we have appended the extension.
									if(resource_manager->getLocalAbsPathForResource(*resource).size() >= 260)
										resource->setRawLocalPath(resource_manager->computeRawLocalPathFromURLHash(resource->URL, extension)); // Computes a path that doesn't contain the filename, just uses a hash of the filename.

									if(VERBOSE) conPrint("Added extension to local path, new local path: " + resource_manager->getLocalAbsPathForResource(*resource));
								}

							

							// Save to disk
							const std::string path = resource_manager->getLocalAbsPathForResource(*resource);
							try
							{
								FileUtils::writeEntireFile(path, (const char*)data.data(), data.size());

								if(VERBOSE) conPrint("NetDownloadResourcesThread: Wrote downloaded file to '" + path + "'. (len=" + toString(data.size()) + ") ");

								resource->setState(Resource::State_Present);
								resource_manager->markAsChanged();

								out_msg_queue->enqueue(new ResourceDownloadedMessage(url, resource));
							}
							catch(FileUtils::FileUtilsExcep& e)
							{
								resource->setState(Resource::State_NotPresent);
								resource_manager->markAsChanged();
								if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while writing file to disk: " + e.what());
							}
						}
						else
							throw glare::Exception("Unknown protocol scheme in URL '" + toStdString(url) + "': '" + url_components.scheme + "'");
					}
					catch(glare::Exception& e)
					{
						resource->setState(Resource::State_NotPresent);
						resource_manager->markAsChanged();
						if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while downloading file: " + e.what());
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("NetDownloadResourcesThread glare::Exception: " + e.what());
	}
#endif
}
