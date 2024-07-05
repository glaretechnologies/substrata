/*=====================================================================
MainPageHandlers.h
-------------------
Copyright Glare Technologies Limited 2021 -
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
MainPageHandlers
----------------

=====================================================================*/
namespace MainPageHandlers
{
	void renderRootPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderTermsOfUse(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAboutParcelSales(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderFAQ(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderRunningYourOwnServerPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAboutScripting(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAboutLuauScripting(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderAboutSubstrataPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderNotFoundPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderBotStatusPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	void renderMapPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
} 
