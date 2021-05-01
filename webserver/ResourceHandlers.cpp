/*=====================================================================
ResourceHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ResourceHandlers.h"


#include "RequestInfo.h"
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "../server/ServerWorldState.h"
#include "../server/Order.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <MemMappedFile.h>


namespace ResourceHandlers
{


void handleResourceRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const std::string resource_URL = web::Escaping::URLUnescape(::eatPrefix(request.path, "/resource/"));

		// TEMP: print out request and headers
		conPrint("handleResourceRequest: resource_URL: " + resource_URL);
		for(size_t i=0; i<request.headers.size(); ++i)
			conPrint("\theader: " + request.headers[i].key + " : " + request.headers[i].value);
		

		// Lookup resource manager to see if we have a resource for this URL.  If so, set local_path to the local path of the resource.
		std::string local_path;
		{
			Lock lock(world_state.resource_manager->getMutex());

			auto res = world_state.resource_manager->getResourcesForURL().find(resource_URL);
			if(res != world_state.resource_manager->getResourcesForURL().end())
			{
				ResourceRef resource = res->second;
				if(resource->getState() == Resource::State_Present)
				{
					local_path = resource->getLocalPath();
				}
			}
		} // End lock scope

		if(!local_path.empty())
		{
			// Resource is present, send it
			try
			{
				// Since resources have content hashes in URLs, they content for a given resource doesn't change.
				// Therefore we can always return HTTP 304 not modified responses.
				for(size_t i=0; i<request.headers.size(); ++i)
					if(StringUtils::equalCaseInsensitive(request.headers[i].key, "if-modified-since"))
					{
						conPrint("returning 304 Not Modified...");
						
						const std::string response = 
							"HTTP/1.1 304 Not Modified\r\n"
							"Connection: Keep-Alive\r\n"
							"\r\n";
				
						reply_info.socket->writeData(response.c_str(), response.size());
						return;
					}


				const std::string content_type = web::ResponseUtils::getContentTypeForPath(local_path); // Guess content type

				MemMappedFile file(local_path);

				// NOTE: only handle a single range for now, because the response content types (and encoding?) get different for multiple ranges.
				if(request.ranges.size() == 1)
				{
					for(size_t i=0; i<request.ranges.size(); ++i)
					{
						const web::Range range = request.ranges[i];
						if(range.start < 0 || range.start >= (int64)file.fileSize())
							throw glare::Exception("invalid range");
						
						int64 range_size;
						if(range.end_incl == -1) // if this range is just to the end:
							range_size = (int64)file.fileSize() - range.start;
						else
						{
							if(range.start > range.end_incl)
								throw glare::Exception("invalid range");
							range_size = range.end_incl - range.start + 1;
						}

						const int64 use_range_end = range.start + range_size;
						if(use_range_end > (int64)file.fileSize())
							throw glare::Exception("invalid range");

						conPrint("\thandleResourceRequest: serving data range (start: " + toString(range.start) + ", range_size: " + toString(range_size) + ")");
				
						const std::string response = 
							"HTTP/1.1 206 Partial Content\r\n"
							"Content-Type: " + content_type + "\r\n"
							"Content-Range: bytes " + toString(range.start) + "-" + toString(use_range_end - 1) + "/" + toString(file.fileSize()) + "\r\n" // Note that ranges are inclusive, hence the - 1.
							"Cache-Control: max-age=100000000\r\n"
							"Connection: Keep-Alive\r\n"
							"Content-Length: " + toString(range_size) + "\r\n"
							"\r\n";

						reply_info.socket->writeData(response.c_str(), response.size());

						// Sanity check range.start and range_size.  Should be valid by here.
						assert((range.start >= 0) && (range.start <= (int64)file.fileSize()) && (range.start + range_size <= (int64)file.fileSize()));
						if(!(range.start >= 0) && (range.start <= (int64)file.fileSize()) && (range.start + range_size <= (int64)file.fileSize()))
							throw glare::Exception("internal error computing ranges");

						reply_info.socket->writeData((const uint8*)file.fileData() + range.start, range_size);
				
						conPrint("\thandleResourceRequest: sent data range. (len: " + toString(range_size) + ")");
					}
				}
				else
				{
					conPrint("\thandleResourceRequest: serving data (len: " + toString(file.fileSize()) + ")");

					web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, file.fileData(), file.fileSize(), content_type.c_str());

					conPrint("\thandleResourceRequest: sent data. (len: " + toString(file.fileSize()) + ")");
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Error while handling resource request: " + e.what());
				web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "resource not found.");
				return;
			}
		}
		else
			web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "resource not found.");
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error while returning resource.");
	}
}


} // end namespace ResourceHandlers
