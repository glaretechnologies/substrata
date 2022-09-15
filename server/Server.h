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

	Reference<ServerAllWorldsState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	std::string screenshot_dir;

	ServerConfig config;
};
