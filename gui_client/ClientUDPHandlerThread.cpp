/*=====================================================================
ClientUDPHandlerThread.cpp
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ClientUDPHandlerThread.h"


#include "WorldState.h"
#include "../webserver/LoginHandlers.h"
#include "../shared/Protocol.h"
#include "../shared/ProtocolStructs.h"
#include "../shared/UID.h"
#include "../shared/WorldObject.h"
#include "../shared/MessageUtils.h"
#include "../shared/FileTypes.h"
#include <vec3.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <MySocket.h>
#include <URL.h>
#include <Lock.h>
#include <StringUtils.h>
#include <CryptoRNG.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <FileUtils.h>
#include <MemMappedFile.h>
#include <FileOutStream.h>
#include <networking/RecordingSocket.h>
#include <maths/CheckedMaths.h>
#include <openssl/err.h>
#include <algorithm>
#include <RuntimeCheck.h>
#include <Timer.h>
#include <Networking.h>
#include <opus.h>


ClientUDPHandlerThread::ClientUDPHandlerThread(Reference<UDPSocket> udp_socket_, const std::string& server_hostname_, WorldState* world_state_, glare::AudioEngine* audio_engine_)
:	udp_socket(udp_socket_),
	server_hostname(server_hostname_),
	world_state(world_state_),
	audio_engine(audio_engine_)
{
}


ClientUDPHandlerThread::~ClientUDPHandlerThread()
{
	conPrint("~ClientUDPHandlerThread()");
}


struct AvatarVoiceStreamInfo
{
	Reference<glare::AudioSource> avatar_audio_source;
	OpusDecoder* opus_decoder;
};


void ClientUDPHandlerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientUDPHandlerThread");

	std::unordered_map<uint32, AvatarVoiceStreamInfo> avatar_stream_info; // Map from avatar UID to AvatarVoiceStreamInfo for that avatar.

	try
	{
		// This DNS lookup has already been done in ClientThread, but it should be cached, so we can efficiently do it again here.
		const std::vector<IPAddress> server_ips = Networking::doDNSLookup(server_hostname);
		const IPAddress server_ip_addr = server_ips[0];

		std::vector<uint8> packet_buf(4096);
		std::vector<float> pcm_buffer(480);

		uint32 next_seq_num_expected = 0;

		while(die == 0)
		{
			IPAddress sender_ip_addr;
			int sender_port;
			const size_t packet_len = udp_socket->readPacket(packet_buf.data(), packet_buf.size(), sender_ip_addr, sender_port);

			// conPrint("ClientUDPHandlerThread: Received packet of length " + toString(packet_len) + " from " + sender_ip_addr.toString() + ", port " + toString(sender_port));

			if(world_state->avatars_changed)
			{
				Lock lock(world_state->mutex);

				for(auto it = world_state->avatars.begin(); it != world_state->avatars.end(); ++it)
				{
					Avatar* av = it->second.ptr();
					if(avatar_stream_info.find((uint32)av->uid.value()) == avatar_stream_info.end())
					{
						conPrint("Creating Opus decoder for avatar");

						int opus_error = 0;
						OpusDecoder* opus_decoder = opus_decoder_create(
							48000, // sampling rate
							1, // channels
							&opus_error
						);
						if(opus_error != OPUS_OK)
							throw glare::Exception("opus_decoder_create failed.");

						avatar_stream_info[(uint32)av->uid.value()] = AvatarVoiceStreamInfo({av->audio_source, opus_decoder});
					}
				}

				for(auto it = avatar_stream_info.begin(); it != avatar_stream_info.end();)
				{
					const UID avatar_uid(it->first);

					if(world_state->avatars.count(avatar_uid) == 0) // If the avatar no longer exists:
					{
						opus_decoder_destroy(it->second.opus_decoder);

						it = avatar_stream_info.erase(it); // Remove from our stream info map
					}
					else
						++it;
				}

				world_state->avatars_changed = 0;
			}


			if(sender_ip_addr == server_ip_addr)
			{
				if(packet_len >= 4)
				{
					uint32 type;
					std::memcpy(&type, packet_buf.data(), 4);
					if(type == 1) // If packet has voice type:
					{
						if(packet_len >= 12)
						{
							uint32 avatar_id;
							std::memcpy(&avatar_id, packet_buf.data() + 4, 4);

							// Lookup VoiceChatStreamInfo from avatar_id
							auto res = avatar_stream_info.find(avatar_id);
							if(res != avatar_stream_info.end())
							{
								AvatarVoiceStreamInfo* stream_info = &res->second;

								uint32 rcvd_seq_num;
								std::memcpy(&rcvd_seq_num, packet_buf.data() + 8, 4);

								//conPrint("Received voice packet for avatar (UID: " + toString(avatar_id) + ", seq num: " + toString(rcvd_seq_num) + ")");

								if(rcvd_seq_num < next_seq_num_expected)
								{
									// Discard packet
									conPrint("Discarding packet.");
								}
								else // else seq_num >= next_seq_num_expected
								{
									/*
									while(next_seq_num_expected < rcvd_seq_num)
									{
										conPrint("Packet was missed, doing loss concealment...");
										// We received a packet with a sequence number (rcvd_seq_num) greater than the one we were expecting (next_seq_num_expected).  Treat the packets with sequence number < rcvd_seq_num as lost.
										// Tell Opus we had a missing packet.
										// "Lost packets can be replaced with loss concealment by calling the decoder with a null pointer and zero length for the missing packet."  https://opus-codec.org/docs/opus_api-1.3.1/group__opus__decoder.html

										// "For the PLC and FEC cases, frame_size must be a multiple of 2.5 ms."
										// at 48000 hz, 1 sample = 1 / 48000 s^-1 = 2.08333 e-5 s
										// samples per 2.5 ms = 0.0025 s / 2.08333 e-5 s = 120
										const int num_samples_decoded = opus_decode_float(stream_info->opus_decoder, NULL, 0, pcm_buffer.data(), 480,//(int)pcm_buffer.size(), 
											0 // decode_fec
										);
										if(num_samples_decoded < 0)
										{
											conPrint("Opus decoding failed: " + toString(num_samples_decoded));
										}
										next_seq_num_expected++;
									}
									assert(rcvd_seq_num == next_seq_num_expected);
									*/

									// Decode opus packet
									const size_t packet_header_size_B = 12;
									const size_t opus_packet_len = packet_len - packet_header_size_B;
									const int num_samples_decoded = opus_decode_float(stream_info->opus_decoder, packet_buf.data() + packet_header_size_B, (int32)opus_packet_len, pcm_buffer.data(), (int)pcm_buffer.size(), 
										0 // decode_fec
									);
									if(num_samples_decoded < 0)
									{
										conPrint("Opus decoding failed: " + toString(num_samples_decoded));
									}
									else
									{
										// Append to audio source buffer
										Lock lock(audio_engine->mutex);

										// If too much data is queued up for this audio source:
										if(stream_info->avatar_audio_source->buffer.size() > 4096) // 4096 samples ~= 85 ms at 48 khz
										{
											// Pop all but 2048 items from the buffer.
											const size_t num_samples_to_remove = stream_info->avatar_audio_source->buffer.size() - 2048;
											conPrint("Audio source buffer too full, removing " + toString(num_samples_to_remove) + " samples");

											stream_info->avatar_audio_source->buffer.popFrontNItems(num_samples_to_remove);
										}

										stream_info->avatar_audio_source->buffer.pushBackNItems(pcm_buffer.data(), num_samples_decoded);
									}

									// conPrint("ClientUDPHandlerThread: decoded " + toString(num_samples_decoded) + " samples.");

									next_seq_num_expected++;
								}
							}
							else
							{
								conPrint("Received voice packet for avatar without streaming context. UID: " + toString(avatar_id));
							}
						}
					}
				}
			}
		}
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_BlockingCallCancelled)
		{
			// This is expected when we close the socket from asyncProcedure().
			conPrint("ClientUDPHandlerThread: caught expected ExcepType_BlockingCallCancelled");
		}
		else
			conPrint("ClientUDPHandlerThread: MySocketExcep: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("ClientUDPHandlerThread: glare::Exception: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("ClientUDPHandlerThread: Caught std::bad_alloc.");
	}

	// Destroy Opus decoders
	for(auto it = avatar_stream_info.begin(); it != avatar_stream_info.end(); ++it)
		opus_decoder_destroy(it->second.opus_decoder);

	udp_socket = NULL;
}


// This executes in the ClientUDPHandlerThread.
// We call closesocket() on the UDP socket.  This results in the blocking recvfrom() call returning with WSAEINTR ('blocking operation was interrupted by a call to WSACancelBlockingCall')
static void asyncProcedure(uint64 data)
{
	ClientUDPHandlerThread* udp_handler_thread = (ClientUDPHandlerThread*)data;
	if(udp_handler_thread->udp_socket.nonNull())
		udp_handler_thread->udp_socket->closeSocket();

	udp_handler_thread->decRefCount();
}


void ClientUDPHandlerThread::kill()
{
	die = 1;
	
#if defined(_WIN32)
	this->incRefCount();
	QueueUserAPC(asyncProcedure, this->getHandle(), /*data=*/(ULONG_PTR)this);
#else
	if(udp_socket.nonNull())
		udp_socket->closeSocket();
#endif
}
