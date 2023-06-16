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


class AudioStreamToServerStartedMessage : public ThreadMessage
{
public:
	AudioStreamToServerStartedMessage(uint32 sampling_rate_) : sampling_rate(sampling_rate_) {}
	
	uint32 sampling_rate;
};


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
	MicReadThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<UDPSocket> udp_socket, UID client_avatar_uid, const std::string& server_hostname, int server_port,
		const std::string& input_device_name);
	~MicReadThread();

	virtual void doRun() override;

	virtual void kill() override { die = 1; }

private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	glare::AtomicInt die;
	Reference<UDPSocket> udp_socket;
	UID client_avatar_uid;
	std::string server_hostname;
	int server_port;
	std::string input_device_name;
};


} // end namespace glare
