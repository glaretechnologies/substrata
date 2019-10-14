/*=====================================================================
CryptoVoxelsLoader.h
--------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "../gui_client/WorldState.h"
#include <MessageableThread.h>
class ClientThread;


// Updates CV data from cryptovoxels.com every N seconds.
class CryptoVoxelsLoaderThread : public MessageableThread
{
public:
	CryptoVoxelsLoaderThread(Reference<WorldState> world_state_) : world_state(world_state_) {}

	virtual void doRun();

	virtual void kill();

	Reference<WorldState> world_state;
};


namespace CryptoVoxelsLoader
{
	void loadCryptoVoxelsData(WorldState& world_state, Reference<ClientThread>& client_thread, Reference<ResourceManager>& resource_manager);
}
