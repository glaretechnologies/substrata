/*=====================================================================
CryptoVoxelsLoader.h
--------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include <MessageableThread.h>


// Updates CV data from cryptovoxels.com every N seconds.
class CryptoVoxelsLoaderThread : public MessageableThread
{
public:
	CryptoVoxelsLoaderThread(Reference<ServerWorldState> world_state_) : world_state(world_state_) {}

	virtual void doRun();

	virtual void kill();

	Reference<ServerWorldState> world_state;
};


namespace CryptoVoxelsLoader
{
	void loadCryptoVoxelsData(ServerWorldState& world_state);
}
