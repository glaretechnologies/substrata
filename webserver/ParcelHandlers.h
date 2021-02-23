/*=====================================================================
ParcelHandlers.h
-----------------
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
ParcelHandlers
---------------

=====================================================================*/
namespace ParcelHandlers
{
	void renderParcelPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Shows parcel details
} 
