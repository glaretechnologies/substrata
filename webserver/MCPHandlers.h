/*=====================================================================
MCPHandlers.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include <string>
#include <vector>


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

Creating a voxel object:

curl.exe -k -X POST https://localhost/mcp -H "Authorization: Bearer YOUR_API_KEY" -H "Content-Type: application/json" -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"create_voxel_object\",\"argumen
  ts\":{\"pos\":{\"x\":0,\"y\":0,\"z\":0},\"materials\":[{\"colour_rgb\":{\"r\":0.2,\"g\":0.4,\"b\":0.9}},{\"colour_rgb\":{\"r\":0.9,\"g\":0.2,\"b\":0.1}}],\"voxels\":[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0,\"mat\":1}]}}}'

=====================================================================*/
namespace MCPHandlers
{
	// Handles a POST request to /mcp.
	void handleMCPRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);


	struct ToolResult
	{
		ToolResult() : is_error(false) {}
		std::string text;
		bool is_error;
	};

	// Call a tool directly, in-process, without going via HTTP or JSON-RPC.  Used by the in-world Builder AI.
	// The caller is responsible for authenticating the user; the tools act as acting_user_id and are subject to that user's permissions.
	// args_json is the raw JSON object of tool arguments, e.g. {"pos":{"x":0,"y":0,"z":1}}.  May be empty for a tool taking no arguments.
	// Does not throw: tool failures are returned with is_error set.
	// NOTE: the caller must NOT hold the world state lock, as the tools take it themselves.
	ToolResult callTool(ServerAllWorldsState& world_state, const std::string& tool_name, const std::string& args_json, const UserID acting_user_id, const std::string& acting_user_name);

	struct ToolSpec
	{
		std::string name;
		std::string description;
		std::string input_schema_json; // The JSON schema for the tool's arguments, as raw JSON text.
	};

	// Get the list of tools in a form suitable for passing to an LLM API.
	// This is the same list served by tools/list, so that the in-world Builder AI and external MCP agents always see the same tools.
	void getToolSpecs(std::vector<ToolSpec>& specs_out);
}
