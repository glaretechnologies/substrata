/*=====================================================================
MeshLODGenThread.h
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <MessageableThread.h>
#include <AtomicInt.h>
class Server;
class ServerAllWorldsState;


class CheckGenResourcesForObject : public ThreadMessage
{
public:
	UID ob_uid;
};


class CheckGenLodResourcesForURL : public ThreadMessage
{
public:
	CheckGenLodResourcesForURL(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


class NewResourceGenerated : public ThreadMessage
{
public:
	NewResourceGenerated(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


/*=====================================================================
MeshLODGenThread
----------------
Does generation of LOD meshes, also LOD textures and Basis textures.

Lightmap LOD generation is done by LightMapperBot.
=====================================================================*/
class MeshLODGenThread : public MessageableThread
{
public:
	MeshLODGenThread(Server* server, ServerAllWorldsState* world_state);

	virtual ~MeshLODGenThread();

	virtual void doRun() override;

	virtual void kill() override;

private:
	Server* server;
	ServerAllWorldsState* world_state;
	glare::AtomicInt should_quit;
};
