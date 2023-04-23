/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include "ThreadManager.h"
#include "../shared/ResourceManager.h"
#include <IPAddress.h>
class WorkerThread;


class ServerConfig
{
public:
	ServerConfig() : allow_light_mapper_bot_full_perms(false) {}
	
	std::string webserver_fragments_dir; // empty string = use default.
	std::string webserver_public_files_dir; // empty string = use default.
	std::string webclient_dir; // empty string = use default.
	
	bool allow_light_mapper_bot_full_perms; // Allow lightmapper bot (User account with name "lightmapperbot" to have full write permissions.
};


struct ServerConnectedClientInfo
{
	IPAddress ip_addr;
	int client_UDP_port; // UDP port on client end
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


	// Called from off main thread
	void clientUDPPortOpen(WorkerThread* worker_thread, const IPAddress& ip_addr, int client_UDP_port);
	void clientDisconnected(WorkerThread* worker_thread);


	Reference<ServerAllWorldsState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	ThreadManager mesh_lod_gen_thread_manager;

	ThreadManager udp_handler_thread_manager;

	std::string screenshot_dir;

	ServerConfig config;

	Mutex connected_clients_mutex;
	std::map<WorkerThread*, ServerConnectedClientInfo> connected_clients;
	glare::AtomicInt connected_clients_changed;
};
