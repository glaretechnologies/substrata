/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include "ThreadManager.h"
#include "../shared/ResourceManager.h"


class ServerConfig
{
public:
	ServerConfig() : allow_light_mapper_bot_full_perms(false) {}
	
	std::string webserver_fragments_dir; // empty string = use default.
	std::string webserver_public_files_dir; // empty string = use default.
	std::string webclient_dir; // empty string = use default.
	
	bool allow_light_mapper_bot_full_perms; // Allow lightmapper bot (User account with name "lightmapperbot" to have full write permissions.
};


/*=====================================================================
Server
--------------------
=====================================================================*/
class Server
{
public:
	Server();

	double getCurrentGlobalTime() const;

	void enqueueMsgForLodGenThread(ThreadMessageRef msg) { mesh_lod_gen_thread_manager.enqueueMessage(msg); }

	Reference<ServerAllWorldsState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	ThreadManager mesh_lod_gen_thread_manager;

	std::string screenshot_dir;

	ServerConfig config;
};
