/*=====================================================================
ClientUDPHandlerThread.h
------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <RequestInfo.h>
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <UDPSocket.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include <BufferInStream.h>
#include <string>
#include "../audio/AudioEngine.h"
class WorldState;


/*=====================================================================
ClientUDPHandlerThread
----------------------
=====================================================================*/
class ClientUDPHandlerThread : public MessageableThread
{
public:
	ClientUDPHandlerThread(Reference<UDPSocket> udp_socket, const std::string& server_hostname, WorldState* world_state, glare::AudioEngine* audio_engine);
	virtual ~ClientUDPHandlerThread();

	virtual void doRun() override;

	virtual void kill() override;

//private:
	glare::AtomicInt die;
	Reference<UDPSocket> udp_socket;
	std::string server_hostname;

	WorldState* world_state;
	glare::AudioEngine* audio_engine;
};
