/*=====================================================================
WebServerResponseUtils.h
------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <string>
class ServerAllWorldsState;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
ResponseUtils
-------------------

=====================================================================*/
namespace WebServerResponseUtils
{
	const std::string standardHTMLHeader(const web::RequestInfo& request_info, const std::string& page_title, const std::string& extra_header_tags = "");
	const std::string standardHeader(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, const std::string& page_title, const std::string& extra_header_tags = "");

	const std::string standardFooter(const web::RequestInfo& request_info, bool include_email_link);
}
