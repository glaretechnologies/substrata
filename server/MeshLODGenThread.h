/*=====================================================================
MeshLODGenThread.h
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <MessageableThread.h>
class ServerAllWorldsState;


class CheckGenResourcesForObject : public ThreadMessage
{
public:
	UID ob_uid;
};


/*=====================================================================
MeshLODGenThread
----------------
Does generation of LOD meshes, also LOD textures and KTX textures.

Lightmap LOD generation is done by LightMapperBot.
=====================================================================*/
class MeshLODGenThread : public MessageableThread
{
public:
	MeshLODGenThread(ServerAllWorldsState* world_state);

	virtual ~MeshLODGenThread();

	virtual void doRun();

private:
	ServerAllWorldsState* world_state;
};
