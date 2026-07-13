/*=====================================================================
MCPHandlers.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


class ServerAllWorldsState;
namespace web
{
class RequestInfo;
class ReplyInfo;
}


/*=====================================================================
MCPHandlers
-----------
Handles requests to the /mcp endpoint, which implements a Model Context Protocol
(MCP) server over the Streamable HTTP transport (JSON-RPC 2.0).

This allows external AI agents to query and modify the Substrata world.

Requests are authenticated with a per-user API key, supplied in the HTTP
Authorization header ("Authorization: Bearer <key>").  Users create API keys on
their account page (see AccountHandlers).  World-mutation tools act as the user
that owns the key, and are subject to that user's object/parcel permissions.
=====================================================================*/
namespace MCPHandlers
{
	// Handles a POST request to /mcp.
	void handleMCPRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
