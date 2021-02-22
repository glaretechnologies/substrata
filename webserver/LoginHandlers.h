/*=====================================================================
LoginHandlers.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
class User;


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
	bool isLoggedIn(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out); // Locks ServerAllWorldsState

	// ServerAllWorldsState should be locked
	User* getLoggedInUser(ServerAllWorldsState& world_state, const web::RequestInfo& request_info);

	void renderLoginPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLogoutPost(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderSignUpPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleSignUpPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
