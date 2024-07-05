/*=====================================================================
LoginHandlers.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../server/ServerWorldState.h"
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
	bool isLoggedIn(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out,
		bool& is_user_admin_out); // Locks ServerAllWorldsState

	bool loggedInUserHasAdminPrivs(ServerAllWorldsState& world_state, const web::RequestInfo& request_info);


	// Returns NULL if not logged in as a valid user.
	// ServerAllWorldsState should be locked
	User* getLoggedInUser(ServerAllWorldsState& world_state, const web::RequestInfo& request_info) REQUIRES(world_state.mutex);

	void renderLoginPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleLogoutPost(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderSignUpPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleSignUpPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderResetPasswordPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleResetPasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void renderResetPasswordFromEmailPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleSetNewPasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderChangePasswordPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	void handleChangePasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
