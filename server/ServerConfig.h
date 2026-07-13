/*=====================================================================
ServerConfig.h
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once

#include <string>
#include <map>


class ServerConfig
{
public:
	ServerConfig() : allow_light_mapper_bot_full_perms(false), update_parcel_sales(false), do_lua_http_request_rate_limiting(true), enable_LOD_chunking(true), enable_registration(true), enable_mcp_server(true), do_mcp_rate_limiting(true) {}
	
	std::string webserver_fragments_dir; // empty string = use default.
	std::string webserver_public_files_dir; // empty string = use default.
	std::string webclient_dir; // empty string = use default.

	std::string tls_certificate_path; // empty string = use default.
	std::string tls_private_key_path; // empty string = use default.
	
	bool allow_light_mapper_bot_full_perms; // Allow lightmapper bot (User account with name "lightmapperbot") to have full write permissions.

	bool update_parcel_sales; // Should we run auctions?

	bool do_lua_http_request_rate_limiting; // Should we rate-limit HTTP requests made by Lua scripts?

	bool enable_LOD_chunking; // Should we generate LOD chunks?

	bool enable_registration; // Should we allow new users to register?

	bool enable_mcp_server; // Should the MCP (Model Context Protocol) server endpoint at /mcp be enabled?  Requests are authenticated with a per-user API key; world-mutation tools act as the key's owner, subject to that user's permissions.

	bool do_mcp_rate_limiting; // Should we rate-limit requests to the MCP endpoint (per API-key owner)?

	std::string AI_model_id; // Default value = "xai/grok-4.5"
	std::string shared_LLM_prompt_part; // Default value = "You are a helpful bot in the Substrata Metaverse." etc..  See parseServerConfig in server.cpp for the default.
};


struct ServerCredentials
{
	std::map<std::string, std::string> creds;
};
