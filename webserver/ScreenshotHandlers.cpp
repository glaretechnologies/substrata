/*=====================================================================
ScreenshotHandlers.cpp
----------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ScreenshotHandlers.h"


#include "RequestInfo.h"
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <ConPrint.h>
#include <Parser.h>
#include <MemMappedFile.h>


namespace ScreenshotHandlers
{


void handleScreenshotRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows order details
{
	try
	{
		// Parse screenshot id from request path
		Parser parser(request.path.c_str(), request.path.size());
		if(!parser.parseString("/screenshot/"))
			throw glare::Exception("Failed to parse /screenshot/");

		uint32 screenshot_id;
		if(!parser.parseUnsignedInt(screenshot_id))
			throw glare::Exception("Failed to parse screenshot_id");

		// Get screenshot local path
		std::string local_path;
		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.screenshots.find(screenshot_id);
			if(res == world_state.screenshots.end())
				throw glare::Exception("Couldn't find screenshot");

			local_path = res->second->local_path;
		} // end lock scope


		try
		{
			MemMappedFile file(local_path); // Load screenshot file
			std::string content_type;
			if(::hasExtension(local_path, "jpg"))
				content_type = "image/jpeg";
			else
				content_type = "bleh";

			// Send it to client
			web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheMaxAge(reply_info, file.fileData(), file.fileSize(), content_type, 3600*24*14); // cache max age = 2 weeks
		}
		catch(glare::Exception&)
		{
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load file '" + local_path + "'.");
		}
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace ScreenshotHandlers
