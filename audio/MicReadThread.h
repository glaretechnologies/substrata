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
	AudioStreamToServerStartedMessage(uint32 sampling_rate_, uint32 flags_, uint32 stream_id_) : sampling_rate(sampling_rate_), flags(flags_), stream_id(stream_id_) {}
	uint32 sampling_rate;
	uint32 flags;
	uint32 stream_id;
};


class AudioStreamToServerEndedMessage : public ThreadMessage
{
public:
	AudioStreamToServerEndedMessage() {}
};


class InputVolumeScaleChangedMessage : public ThreadMessage
{
public:
	InputVolumeScaleChangedMessage(float input_vol_scale_factor_) : input_vol_scale_factor(input_vol_scale_factor_) {}
	float input_vol_scale_factor;
};


struct MicReadStatus
{
	MicReadStatus() : cur_level(0) {}

	Mutex mutex;
	float cur_level; // in [0, 1].
};


namespace glare
{


/*=====================================================================
MicReadThread
-------------
Reads audio from microphone, encodes with Opus, streams to server over UDP connection.
=====================================================================*/
class MicReadThread : public MessageableThread
{
public:
	MicReadThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, Reference<UDPSocket> udp_socket, UID client_avatar_uid, const std::string& server_hostname, int server_port,
		const std::string& input_device_name, float input_vol_scale_factor, MicReadStatus* mic_read_status);
	~MicReadThread();

	virtual void doRun() override;

	virtual void kill() override { die = 1; }

//private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	glare::AtomicInt die;
	Reference<UDPSocket> udp_socket;
	UID client_avatar_uid;
	std::string server_hostname;
	int server_port;
	std::string input_device_name;

	float input_vol_scale_factor;

	Mutex buffer_mutex;
	std::vector<float> callback_buffer;

	MicReadStatus* mic_read_status;
};


} // end namespace glare
