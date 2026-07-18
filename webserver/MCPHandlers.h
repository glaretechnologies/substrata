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

To test:
In Powershell:
curl.exe --insecure -v -s https://localhost/mcp -H "Authorization: Bearer YOUR_API_KEY " -H "Content-Type: application/json" -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}'

(--insecure is so curl will accept a self-signed cert, which localhost may use for testing)

or testing Substrata-Login auth type:

curl.exe --insecure -v -s https://localhost/mcp -H "Authorization: Substrata-Login aaaa.bbbb" -H "Content-Type: application/json" -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}'

Creating an object:

curl.exe -k -X POST https://localhost/mcp -H "Authorization: Bearer YOUR_API_KEY" -H "Content-Type: application/json" `
    -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"create_object\",\"arguments\":{\"x\":0,\"y\":0,\"z\":1,\"model_url\":\"Platonic_Solid_obj_5373640347617450145.bmesh\"}}}'

=====================================================================*/
namespace MCPHandlers
{
	// Handles a POST request to /mcp.
	void handleMCPRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
