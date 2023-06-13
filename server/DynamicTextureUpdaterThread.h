/*=====================================================================
DynamicTextureUpdaterThread.h
-----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <MessageableThread.h>
class Server;
class ServerAllWorldsState;


/*=====================================================================
DynamicTextureUpdaterThread
---------------------------
Handles server-side 'dynamic_texture_update' scripts.
These scripts download an image from a fixed URL periodically,
and if the image changes, add it as a resource to the substrata server,
and assign the image to the specified object material.

Note that this code runs on the server, so we have to be a bit careful with it.
=====================================================================*/
class DynamicTextureUpdaterThread : public MessageableThread
{
public:
	DynamicTextureUpdaterThread(Server* server, ServerAllWorldsState* world_state);

	virtual ~DynamicTextureUpdaterThread();

	virtual void doRun();

private:
	Server* server;
	ServerAllWorldsState* world_state;
};
