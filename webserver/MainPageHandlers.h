/*=====================================================================
MainPageHandlers.h
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-23 22:28:10 +0100
=====================================================================*/
#pragma once

//
//class RequestInfo;
//class DataStore;
//class ReplyInfo;

namespace web
{

class RequestInfo;
class ReplyInfo;

}


/*=====================================================================
BlogHandlers
-------------------

=====================================================================*/
namespace MainPageHandlers
{
	void renderRootPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderNotFoundPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
