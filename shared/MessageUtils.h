/*=====================================================================
MessageUtils.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/SocketBufferOutStream.h>
#include <assert.h>
class OutStream;
class InStream;


/*=====================================================================
MessageUtils
------------

=====================================================================*/
class MessageUtils
{
public:
	static void initPacket(SocketBufferOutStream& scratch_packet, uint32 message_id)
	{
		scratch_packet.buf.resize(sizeof(uint32) * 2);
		std::memcpy(&scratch_packet.buf[0], &message_id, sizeof(uint32));
		std::memset(&scratch_packet.buf[4], 0, sizeof(uint32)); // Write dummy message length, will be updated later when size of message is known.
	}

	static void updatePacketLengthField(SocketBufferOutStream& packet)
	{
		// length field is second uint32
		assert(packet.buf.size() >= sizeof(uint32) * 2);
		if(packet.buf.size() >= sizeof(uint32) * 2)
		{
			const uint32 len = (uint32)packet.buf.size();
			std::memcpy(&packet.buf[4], &len, 4);
		}
	}
};
