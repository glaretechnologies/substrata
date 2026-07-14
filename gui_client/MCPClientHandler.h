/*=====================================================================
MCPClientHandler.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <webserver/RequestHandler.h>
#include <utils/Reference.h>
#include <string>
class MainWindow;
class JSONParser;
class HTTPClient;
struct JSONNode;


/*=====================================================================
MCPClientRequestHandler
-----------------------
Serves an MCP (Model Context Protocol) endpoint over local HTTP from the
gui_client, so an AI agent (e.g. Claude Code) can drive building in the
connected world.

The 'render_view' tool is handled locally (by rendering the connected world from
a given camera - see MainWindow::enqueueMCPRenderRequest).  All other MCP methods
and tool calls are forwarded transparently to the /mcp endpoint of the Substrata
server the client is currently connected to, authenticated with the configured API key.

NOTE: only loopback (localhost) requests are served.
=====================================================================*/
class MCPClientRequestHandler : public web::RequestHandler
{
public:
	MCPClientRequestHandler(MainWindow* main_window, const std::string& api_key);

	virtual void handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info) override;

private:
	void handleRenderView(const JSONParser& parser, const JSONNode& root, web::ReplyInfo& reply_info);
	std::string forwardToServer(const std::string& request_body); // Throws glare::Exception on failure.

	MainWindow* main_window;
	std::string api_key;

	// A request handler is created per incoming connection and used serially by a single worker thread, so http_client
	// needs no locking.  Keep-alive is enabled on it so forwarded calls reuse the TCP (and TLS) connection to the server.
	Reference<HTTPClient> http_client;
};


class MCPClientSharedRequestHandler : public web::SharedRequestHandler
{
public:
	MCPClientSharedRequestHandler(MainWindow* main_window_, const std::string& api_key_)
	:	main_window(main_window_), api_key(api_key_) {}

	virtual Reference<web::RequestHandler> getOrMakeRequestHandler() override { return new MCPClientRequestHandler(main_window, api_key); }

	MainWindow* main_window;
	std::string api_key;
};
