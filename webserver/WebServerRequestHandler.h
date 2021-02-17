/*=====================================================================
WebServerRequestHandler.h
-------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <RequestHandler.h>
class WebDataStore;


/*=====================================================================
BlogRequestHandler
-------------------

=====================================================================*/
class WebServerRequestHandler : public web::RequestHandler
{
public:
	WebServerRequestHandler();
	virtual ~WebServerRequestHandler();

	virtual void handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info);

	virtual void handleWebsocketTextMessage(const std::string& msg, Reference<SocketInterface>& socket, const Reference<WorkerThread>& worker_thread);

	virtual void websocketConnectionClosed(Reference<SocketInterface>& socket, const Reference<WorkerThread>& worker_thread);

	WebDataStore* data_store;
private:
};


class WebServerSharedRequestHandler : public web::SharedRequestHandler
{
public:
	WebServerSharedRequestHandler(){}
	virtual ~WebServerSharedRequestHandler(){}

	virtual Reference<web::RequestHandler> getOrMakeRequestHandler() // Factory method for request handler.
	{
		WebServerRequestHandler* h = new WebServerRequestHandler();
		h->data_store = data_store;
		return h;
	}

	WebDataStore* data_store;
private:
};
