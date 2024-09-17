/*=====================================================================
ChunkGenThread.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
class ServerAllWorldsState;


/*=====================================================================
ChunkGenThread
--------------
Computes world chunks - combines object meshes into one mesh, combines
textures into an array texture.  Simplifies meshes.
=====================================================================*/
class ChunkGenThread : public MessageableThread
{
public:
	ChunkGenThread(ServerAllWorldsState* world_state);

	virtual ~ChunkGenThread();

	virtual void doRun();

private:
	ServerAllWorldsState* world_state;
};
