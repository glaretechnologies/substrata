/*=====================================================================
ClientThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "ClientThread.h"


#include "mysocket.h"
#include "MainWindow.h"
#include <ConPrint.h>
#include "../shared/WorldState.h"
#include <vec3.h>
#include <SocketBufferOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <PlatformUtils.h>


static const bool VERBOSE = false;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& hostname_, int port_, MainWindow* main_window_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	port(port_),
	main_window(main_window_)
{
	socket = new MySocket();
}


ClientThread::~ClientThread()
{
	
}


void ClientThread::killConnection()
{
	if(socket.nonNull())
		socket->ungracefulShutdown();
}


void ClientThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientThread");

	try
	{
		conPrint("ClientThread Connecting to " + hostname + ":" + toString(port) + "...");

//		socket = new MySocket(hostname, port);
		socket->connect(hostname, port);

		conPrint("ClientThread Connected to " + hostname + ":" + toString(port) + "!");

		socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

		// Write connection type
		socket->writeUInt32(ConnectionTypeUpdates);

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
				case ObjectTransformUpdate:
					{
						//conPrint("ObjectTransformUpdate");
						const UID object_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								
								WorldObject* ob = res->second.getPointer();
								if(main_window->selected_ob.getPointer() != ob) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
								{
									ob->pos = pos;
									ob->axis = axis;
									ob->angle = angle;
									ob->from_remote_dirty = true;

									//conPrint("updated object transform");
								}
							}
						}
						break;
					}
				case ObjectCreated:
					{
						conPrint("ObjectCreated");
						const UID object_uid = readUIDFromStream(*socket);
						//const std::string name = socket->readStringLengthFirst(); //TODO: enforce max len
						const std::string model_url = socket->readStringLengthFirst(); //TODO: enforce max len
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing object in world state
						{
							::Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res == world_state->objects.end())
							{
								// Object for UID not already created, create it now.
								WorldObjectRef ob = new WorldObject();
								ob->uid = object_uid;
								//ob->name = name;
								ob->model_url = model_url;
								ob->pos = pos;
								ob->axis = axis;
								ob->angle = angle;
								ob->state = WorldObject::State_JustCreated;
								ob->from_remote_dirty = true;
								world_state->objects.insert(std::make_pair(object_uid, ob));

								conPrint("created new object");
							}
						}
						break;
					}
				case ObjectDestroyed:
					{
						conPrint("ObjectDestroyed");
						const UID object_uid = readUIDFromStream(*socket);

						// Mark object as dead
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								ob->state = WorldObject::State_Dead;
								ob->from_remote_dirty = true;
							}
						}
						break;
					}
				case GetFile:
					{
						conPrint("GetFile");
						const std::string model_url = socket->readStringLengthFirst(); //TODO: enforce max len

						out_msg_queue->enqueue(new GetFileMessage(model_url));
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
