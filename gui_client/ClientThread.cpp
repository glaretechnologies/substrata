/*=====================================================================
ClientThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "ClientThread.h"


#include "mysocket.h"
#include <ConPrint.h>
#include "../shared/WorldState.h"
#include <vec3.h>
#include <SocketBufferOutStream.h>
#include <Exception.h>
#include <StringUtils.h>


static const bool VERBOSE = false;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_)
:	out_msg_queue(out_msg_queue_)
{

}


ClientThread::~ClientThread()
{

}


void ClientThread::run()
{
	try
	{
		const std::string hostname = "217.155.32.43";
		const int port = 7654;

		conPrint("Connecting to " + hostname + ":" + toString(port) + "...");

		MySocketRef socket = new MySocket(hostname, port);

		conPrint("Connected to " + hostname + ":" + toString(port) + "!");

		socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

		// Read assigned client avatar UID
		this->client_avatar_uid = readUIDFromStream(*socket);


		// Send AvatarCreated packet for this client's avatar
		SocketBufferOutStream packet;
		packet.writeUInt32(AvatarCreated);
		writeToStream(client_avatar_uid, packet);
		packet.writeStringLengthFirst("a person");
		packet.writeStringLengthFirst("some model URI");
		writeToStream(Vec3d(0,0,0), packet);
		writeToStream(Vec3f(0,0,1), packet);
		packet.writeFloat(0.f);

		//std::string packet_string(packet.buf.size(), '\0');
		//std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

		socket->writeData(packet.buf.data(), packet.buf.size());



		while(1) // write to / read from socket loop
		{
			// See if we have any pending data to send in the data_to_send queue, and if so, send all pending data.
			if(VERBOSE) conPrint("ClientThread: checking for pending data to send...");
			{
				Lock lock(data_to_send.getMutex());

				while(!data_to_send.unlockedEmpty())
				{
					std::string data;
					data_to_send.unlockedDequeue(data);

					// Write the data to the socket
					if(!data.empty())
					{
						if(VERBOSE) conPrint("ClientThread: calling writeData with data '" + data + "'...");
						socket->writeData(data.data(), data.size());
					}
				}
			}


#if defined(_WIN32) || defined(OSX)
			if(socket->readable(0.05)) // If socket has some data to read from it:
#else
			if(socket->readable(event_fd)) // Block until either the socket is readable or the event fd is signalled, which means we have data to write.
#endif
			{
				// Read msg type
				const uint32 msg_type = socket->readUInt32();
				switch(msg_type)
				{
				case AvatarTransformUpdate:
					{
						//conPrint("AvatarTransformUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->pos = pos;
								avatar->axis = axis;
								avatar->angle = angle;
								avatar->dirty = true;

								//conPrint("updated avatar transform");
							}
						}
						break;
					}
				case AvatarCreated:
					{
						conPrint("AvatarCreated");
						const UID avatar_uid = readUIDFromStream(*socket);
						const std::string name = socket->readStringLengthFirst(); //TODO: enforce max len
						const std::string model_url = socket->readStringLengthFirst(); //TODO: enforce max len
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing avatar in world state
						{
							printVar((uint64)&world_state->mutex);
							::Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res == world_state->avatars.end())
							{
								// Avatar for UID not already created, create it now.
								AvatarRef avatar = new Avatar();
								avatar->uid = avatar_uid;
								avatar->name = name;
								avatar->model_url = model_url;
								avatar->pos = pos;
								avatar->axis = axis;
								avatar->angle = angle;
								avatar->state = Avatar::State_JustCreated;
								avatar->dirty = true;
								world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

								conPrint("created new avatar");
							}
						}
						break;
					}
				case AvatarDestroyed:
					{
						conPrint("AvatarDestroyed");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Mark avatar as dead
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->state = Avatar::State_Dead;
								avatar->dirty = true;
							}
						}
						break;
					}
				case ChatMessageID:
					{
						conPrint("ChatMessage");
						const std::string name = socket->readStringLengthFirst(); //TODO: enforce max len
						const std::string msg = socket->readStringLengthFirst(); //TODO: enforce max len
						out_msg_queue->enqueue(new ChatMessage(name, msg));
						break;
					}
				default:
					{
						conPrint("Unknown message id: " + ::toString(msg_type));
					}
				}
			}
			else
			{
#if defined(_WIN32) || defined(OSX)
#else
				if(VERBOSE) print("WorkerThread: event FD was signalled.");

				// The event FD was signalled, which means there is some data to send on the socket.
				// Reset the event fd by reading from it.
				event_fd.read();

				if(VERBOSE) print("WorkerThread: event FD has been reset.");
#endif
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
	}
}


void ClientThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("ClientThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}
