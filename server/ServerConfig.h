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
	ServerConfig() : allow_light_mapper_bot_full_perms(false), update_parcel_sales(false), do_lua_http_request_rate_limiting(true), enable_LOD_chunking(true), enable_registration(true) {}
	
	std::string webserver_fragments_dir; // empty string = use default.
	std::string webserver_public_files_dir; // empty string = use default.
	std::string webclient_dir; // empty string = use default.

	std::string tls_certificate_path; // empty string = use default.
	std::string tls_private_key_path; // empty string = use default.
	
	bool allow_light_mapper_bot_full_perms; // Allow lightmapper bot (User account with name "lightmapperbot" to have full write permissions.

	bool update_parcel_sales; // Should we run auctions?

	bool do_lua_http_request_rate_limiting; // Should we rate-limit HTTP requests made by Lua scripts?

	bool enable_LOD_chunking; // Should we generate LOD chunks?

	bool enable_registration; // Should we allow new users to register?

	std::string AI_model_id; // Default value = "xai/grok-4-1-fast-non-reasoning"
	std::string shared_LLM_prompt_part; // Default value = "You are a helpful bot in the Substrata Metaverse." etc..
};


struct ServerCredentials
{
	std::map<std::string, std::string> creds;
};
