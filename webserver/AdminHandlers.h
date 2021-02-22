/*=====================================================================
AdminHandlers.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
AdminHandlers
-------------
For rendering admin pages etc..
=====================================================================*/
namespace AdminHandlers
{
	void renderMainAdminPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
