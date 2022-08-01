/*=====================================================================
ClientSenderThread.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "ClientSenderThread.h"


#include "../shared/Protocol.h"
#include <MySocket.h>
#include <PlatformUtils.h>
#include <ConPrint.h>


ClientSenderThread::ClientSenderThread(Reference<SocketInterface> socket_)
:	socket(socket_)
{}


ClientSenderThread::~ClientSenderThread()
{}


void ClientSenderThread::kill()
{
	should_die = glare::atomic_int(1);
}


void ClientSenderThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientSenderThread");

	try
	{
		while(1) // Loop until an exception is thrown, or should_die is set.
		{
			if(should_die)
			{
				// Send a CyberspaceGoodbye message to the server.
				const uint32 msg_type_and_len[2] = { Protocol::CyberspaceGoodbye, sizeof(uint32) * 2 };
				socket->writeData(msg_type_and_len, sizeof(uint32) * 2);

				socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the server.
				return;
			}

			// See if we have any pending data to send in the data_to_send buffer, and if so, send all pending data.
			// We don't want to do network writes while holding the data_to_send_mutex.  So copy to temp_data_to_send.
			{
				Lock lock(data_to_send_mutex);
				temp_data_to_send = data_to_send;
				data_to_send.clear();
			}

			if(temp_data_to_send.nonEmpty())
			{
				socket->writeData(temp_data_to_send.data(), temp_data_to_send.size());
				temp_data_to_send.clear();
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("ClientSenderThread: Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("ClientSenderThread: glare::Exception: " + e.what());
	}
}


void ClientSenderThread::enqueueDataToSend(const ArrayRef<uint8> data)
{
	if(!data.empty())
	{
		// Append data to data_to_send
		Lock lock(data_to_send_mutex);
		const size_t write_i = data_to_send.size();
		data_to_send.resize(write_i + data.size());
		std::memcpy(&data_to_send[write_i], data.data(), data.size());
	}
}
