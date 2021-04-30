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
				MemMappedFile file(local_path);

				const std::string content_type = web::ResponseUtils::getContentTypeForPath(local_path); // Guess content type

				conPrint("\thandleResourceRequest: serving data (len: " + toString(file.fileSize()) + ")");
				
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, file.fileData(), file.fileSize(), content_type.c_str());
				
				conPrint("\thandleResourceRequest: sent data. (len: " + toString(file.fileSize()) + ")");
			}
			catch(glare::Exception&)
			{
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
