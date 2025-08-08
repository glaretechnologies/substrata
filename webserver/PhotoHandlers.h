/*=====================================================================
PhotoHandlers.h
---------------
Copyright Glare Technologies Limited 2025 -
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
PhotoHandlers
-------------

=====================================================================*/
namespace PhotoHandlers
{
	void handlePhotoImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get a photo
	void handlePhotoMidSizeImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get a photo
	void handlePhotoThumbnailImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get a photo
	void handlePhotoPageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get a photo
} 
