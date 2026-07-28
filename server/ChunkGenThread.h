/*=====================================================================
ChunkGenThread.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
class Server;
class ServerAllWorldsState;


/*=====================================================================
ChunkGenThread
--------------
Computes world LOD chunks - combines object meshes into one mesh, combines
textures into an array texture.  Simplifies meshes.
=====================================================================*/
class ChunkGenThread : public MessageableThread
{
public:
	ChunkGenThread(Server* server, ServerAllWorldsState* all_worlds_state);

	virtual ~ChunkGenThread();

	virtual void doRun();

private:
	Server* server;
	ServerAllWorldsState* all_worlds_state;
};
