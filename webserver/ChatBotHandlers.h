/*=====================================================================
ChatBotHandlers.h
-----------------
Copyright Glare Technologies Limited 2026 -
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
ChatBotHandlers
---------------

=====================================================================*/
namespace ChatBotHandlers
{
	void renderEditChatBotPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderNewChatBotPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);



	void handleNewChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleEditChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleCopyUserAvatarSettingsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	
	void handleUpdateInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
	
	void handleDeleteInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleAddNewInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
