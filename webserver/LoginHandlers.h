/*=====================================================================
LoginHandlers.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;


namespace web
{
class RequestInfo;
class ReplyInfo;
class UnsafeString;
}


/*=====================================================================
LoginHandlers
-------------------

=====================================================================*/
namespace LoginHandlers
{
	bool isLoggedIn(const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out);
	void renderLoginPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLogoutPost(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
