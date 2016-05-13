/*=====================================================================
WorkerThread.cpp
------------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "WorkerThread.h"


#include "../shared/UID.h"
#include <vec3.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <mysocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <ThreadShouldAbortCallback.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include "ServerWorldState.h"
#include "Server.h"


static const bool VERBOSE = false;


WorkerThread::WorkerThread(int thread_id_, const Reference<MySocket>& socket_, Server* server_)
:	thread_id(thread_id_),
	socket(socket_),
	server(server_)
{
	//if(VERBOSE) print("event_fd.efd: " + toString(event_fd.efd));
}


WorkerThread::~WorkerThread()
{
}


void WorkerThread::doRun()
{
	ServerWorldState* world_state = server->world_state.getPointer();

	UID client_avatar_uid = 0;

	try
	{
		socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.


		// Write avatar UID assigned to the connected client.
		
		{
			Lock lock(world_state->mutex);
			client_avatar_uid = world_state->next_avatar_uid;
			world_state->next_avatar_uid = world_state->next_avatar_uid.value() + 1;
		}

		writeToStream(client_avatar_uid, *socket);


		// Send all current avatar data to client
		{
			Lock lock(world_state->mutex);
			for(auto it = world_state->avatars.begin(); it != world_state->avatars.end(); ++it)
			{
				const Avatar* avatar = it->second.getPointer();

				// Send AvatarCreated packet
				SocketBufferOutStream packet;
				packet.writeUInt32(AvatarCreated);
				writeToStream(avatar->uid, packet);
				packet.writeStringLengthFirst(avatar->name);
				packet.writeStringLengthFirst(avatar->model_url);
				writeToStream(avatar->pos, packet);
				writeToStream(avatar->axis, packet);
				packet.writeFloat(avatar->angle);

				socket->writeData(packet.buf.data(), packet.buf.size());
			}
		}


		while(1) // write to / read from socket loop
		{
			// See if we have any pending data to send in the data_to_send queue, and if so, send all pending data.
			if(VERBOSE) conPrint("WorkerThread: checking for pending data to send...");
			{
				Lock lock(data_to_send.getMutex());

				while(!data_to_send.unlockedEmpty())
				{
					std::string data;
					data_to_send.unlockedDequeue(data);

					// Write the data to the socket
					if(!data.empty())
					{
						if(VERBOSE) conPrint("WorkerThread: calling writeWebsocketTextMessage() with data '" + data + "'...");
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
							Lock lock(world_state->mutex);
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
					default:
					{
						conPrint("Unknown message id: " + toString(msg_type));
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

	// Mark avatar corresponding to client as dead
	{
		Lock lock(world_state->mutex);
		if(world_state->avatars.count(client_avatar_uid) == 1)
		{
			world_state->avatars[client_avatar_uid]->state = Avatar::State_Dead;
			world_state->avatars[client_avatar_uid]->dirty = true;
		}
	}
}


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("WorkerThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}
