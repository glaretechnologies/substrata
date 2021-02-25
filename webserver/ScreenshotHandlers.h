/*=====================================================================
ScreenshotHandlers.h
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
class WebDataStore;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
ScreenshotHandlers
------------------

=====================================================================*/
namespace ScreenshotHandlers
{
	void handleScreenshotRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get a screenshot
} 
