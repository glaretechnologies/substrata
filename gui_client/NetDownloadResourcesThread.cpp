/*=====================================================================
NetDownloadResourcesThread.cpp
------------------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "NetDownloadResourcesThread.h"


#include "../shared/Protocol.h"
#include "HTTPClient.h"
#include "url.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>


NetDownloadResourcesThread::NetDownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_)
:	out_msg_queue(out_msg_queue_),
	resource_manager(resource_manager_)
{}


NetDownloadResourcesThread::~NetDownloadResourcesThread()
{}


void NetDownloadResourcesThread::kill()
{
	should_die = 1;
}


static const bool VERBOSE = false;


void NetDownloadResourcesThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("NetDownloadResourcesThread");

	try
	{
		
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

				std::string url = *URLs_to_get.begin();
				URLs_to_get.erase(URLs_to_get.begin());

				ResourceRef resource = resource_manager->getResourceForURL(url);

				// Check to see if we have the resource now, we may have downloaded it recently.
				if(resource->getState() != Resource::State_NotPresent)
				{
					//conPrint("Already have file, not downloading.");
				}
				else
				{
					if(VERBOSE) conPrint("NetDownloadResourcesThread: Downloading file '" + url + "'...");

					resource->setState(Resource::State_Transferring);

					try
					{
						std::string data;

						// Parse URL
						const URL url_components = URL::parseURL(url);
						if(url_components.scheme == "http" || url_components.scheme == "https")
						{
							if(url_components.host == "gateway.ipfs.io")
								throw Indigo::Exception("Skipping " + url);

							// Download with HTTP client
							HTTPClient client;
							HTTPClient::ResponseInfo response_info = client.downloadFile(url, data);
							if(response_info.response_code != 200)
								throw Indigo::Exception("HTTP Download failed: (code: " + toString(response_info.response_code) + "): " + response_info.response_message);

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
								if(!hasExtension(resource->getLocalPath(), extension))
								{
									resource->setLocalPath(resource->getLocalPath() + "." + extension);

									// Avoid path being too long for Windows now that we have appended the extension.
									if(resource->getLocalPath().size() >= 260)
										resource->setLocalPath(resource_manager->computeLocalPathFromURLHash(resource->URL, extension));

									if(VERBOSE) conPrint("Added extension to local path, new local path: " + resource->getLocalPath());
								}

							

							// Save to disk
							const std::string path = resource->getLocalPath();
							try
							{
								FileUtils::writeEntireFile(path, (const char*)data.data(), data.size());

								if(VERBOSE) conPrint("NetDownloadResourcesThread: Wrote downloaded file to '" + path + "'. (len=" + toString(data.size()) + ") ");

								resource->setState(Resource::State_Present);
								resource_manager->markAsChanged();

								out_msg_queue->enqueue(new ResourceDownloadedMessage(url));
							}
							catch(FileUtils::FileUtilsExcep& e)
							{
								resource->setState(Resource::State_NotPresent);
								resource_manager->markAsChanged();
								if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while writing file to disk: " + e.what());
							}
						}
						else
							throw Indigo::Exception("Unknown protocol scheme in URL '" + url + "': '" + url_components.scheme + "'");
					}
					catch(Indigo::Exception& e)
					{
						resource->setState(Resource::State_NotPresent);
						resource_manager->markAsChanged();
						if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while downloading file: " + e.what());
					}
				}
			}
		}
	}
	catch(Indigo::Exception& e)
	{
		conPrint("NetDownloadResourcesThread Indigo::Exception: " + e.what());
	}
}
