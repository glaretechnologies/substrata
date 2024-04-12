/*=====================================================================
NewsPostHandlers.h
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
NewsPostHandlers
----------------

=====================================================================*/
namespace NewsPostHandlers
{
	void renderNewsPostPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAllNewsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderEditNewsPostPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleEditNewsPostPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleDeleteNewsPostPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
