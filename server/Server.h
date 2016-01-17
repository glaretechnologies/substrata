/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "WorldState.h"
#include "ThreadManager.h"


/*=====================================================================
Server
--------------------
=====================================================================*/
class Server
{
public:
	Server();

	Reference<WorldState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;
};
