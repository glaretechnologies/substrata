/*=====================================================================
SubEventHandlers.h
------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
SubEventHandlers
----------------

=====================================================================*/
namespace SubEventHandlers
{
	void renderEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAllEventsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderCreateEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderEditEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleCreateEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleEditEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void handleDeleteEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
