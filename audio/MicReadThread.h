/*=====================================================================
MicReadThread.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <networking/UDPSocket.h>
#include <utils/MessageableThread.h>
#include <utils/AtomicInt.h>
#include <utils/Vector.h>


namespace glare
{


class AudioEngine;


/*=====================================================================
MicReadThread
-------------
Reads audio from microphone, encodes with Opus, streams to server over UDP connection.
=====================================================================*/
class MicReadThread : public MessageableThread
{
public:
	MicReadThread(Reference<UDPSocket> udp_socket, UID client_avatar_uid, const std::string& server_hostname, int server_port);
	~MicReadThread();

	virtual void doRun() override;

	virtual void kill() override { die = 1; }

private:
	glare::AtomicInt die;
	Reference<UDPSocket> udp_socket;
	UID client_avatar_uid;
	std::string server_hostname;
	int server_port;
};


} // end namespace glare
