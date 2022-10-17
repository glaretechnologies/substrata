/*=====================================================================
WebServerRequestHandler.h
-------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <RequestHandler.h>
#include "../shared/UID.h"
#include "../server/User.h"
class WebDataStore;
class ServerAllWorldsState;
class ServerWorldState;
class Server;


class WebServerRequestHandler : public web::RequestHandler
{
public:
	WebServerRequestHandler();
	virtual ~WebServerRequestHandler();

	virtual void handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info) override;

	virtual void handleWebSocketConnection(const web::RequestInfo& request_info, Reference<SocketInterface>& socket) override;

	WebDataStore* data_store;
	Server* server;
	ServerAllWorldsState* world_state;
	bool dev_mode;
private:
};


class WebServerSharedRequestHandler : public web::SharedRequestHandler
{
public:
	WebServerSharedRequestHandler() : dev_mode(false) {}
	virtual ~WebServerSharedRequestHandler(){}

	virtual Reference<web::RequestHandler> getOrMakeRequestHandler() // Factory method for request handler.
	{
		Reference<WebServerRequestHandler> h = new WebServerRequestHandler();
		h->data_store = data_store;
		h->server = server;
		h->world_state = world_state;
		h->dev_mode = dev_mode;
		return h;
	}

	WebDataStore* data_store;
	Server* server;
	ServerAllWorldsState* world_state;
	bool dev_mode;
private:
};
