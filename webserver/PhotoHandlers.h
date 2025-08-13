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
	void handlePhotoImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get the actual photo image
	void handlePhotoMidSizeImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get mid-size photo image
	void handlePhotoThumbnailImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Get thumbnail photo image

	void handlePhotoPageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Show photo page

	void handleDeletePhotoPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info); // Show photo page
} 
