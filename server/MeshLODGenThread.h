/*=====================================================================
MeshLODGenThread.h
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <SocketInterface.h>
#include <set>
#include <string>
#include <vector>
class PrintOutput;
class ThreadMessageSink;
class DataStore;
class ServerAllWorldsState;


/*=====================================================================
MeshLODGenThread
----------------

=====================================================================*/
class MeshLODGenThread : public MessageableThread
{
public:
	// May throw glare::Exception from constructor if EventFD init fails.
	MeshLODGenThread(ServerAllWorldsState* world_state);

	virtual ~MeshLODGenThread();

	virtual void doRun();

private:
	ServerAllWorldsState* world_state;
};
