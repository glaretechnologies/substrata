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

	stuff_to_do_condition.notify();
}


void ClientSenderThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientSenderThread");

	try
	{
		// This code pattern approximately follows ThreadSafeQueue<T>::dequeue().
		while(1)
		{
			{
				Lock lock(mutex);

				while(data_to_send.empty() && !should_die) // While there is nothing to do yet:
				{
					stuff_to_do_condition.wait(mutex); // Suspend until queue is non-empty, or should_die is set, or we get a spurious wake up.
				}

				if(should_die)
					break;

				assert(!data_to_send.empty());

				// We don't want to do network writes while holding the mutex, so copy to temp_data_to_send.
				temp_data_to_send = data_to_send;
				data_to_send.clear();
			} // release mutex

			if(temp_data_to_send.nonEmpty())
			{
				socket->writeData(temp_data_to_send.data(), temp_data_to_send.size());
				temp_data_to_send.clear();
			}
		}

		// Send a CyberspaceGoodbye message to the server.
		const uint32 msg_type_and_len[2] = { Protocol::CyberspaceGoodbye, sizeof(uint32) * 2 };
		socket->writeData(msg_type_and_len, sizeof(uint32) * 2);

		socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the server.
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
		{
			// Append data to data_to_send
			Lock lock(mutex);
			const size_t write_i = data_to_send.size();
			data_to_send.resize(write_i + data.size());
			std::memcpy(&data_to_send[write_i], data.data(), data.size());
		}

		stuff_to_do_condition.notify();
	}
}
