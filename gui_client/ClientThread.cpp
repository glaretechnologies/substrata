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
#include "../shared/Protocol.h"
#include "../shared/WorldState.h"
#include <vec3.h>
#include <SocketBufferOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <PlatformUtils.h>


static const bool VERBOSE = false;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& hostname_, int port_, MainWindow* main_window_,
						   const std::string& avatar_URL_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	port(port_),
	main_window(main_window_),
	avatar_URL(avatar_URL_)
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

		out_msg_queue->enqueue(new ClientConnectingToServerMessage());

		socket->connect(hostname, port);

		conPrint("ClientThread Connected to " + hostname + ":" + toString(port) + "!");

		out_msg_queue->enqueue(new ClientConnectedToServerMessage());

		socket->writeUInt32(CyberspaceHello); // Write hello
		socket->writeUInt32(CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(ConnectionTypeUpdates); // Write connection type

		

		

		const int MAX_STRING_LEN = 10000;

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != CyberspaceHello)
			throw Indigo::Exception("Invalid hello from server: " + toString(hello_response));

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw Indigo::Exception(msg);
		}
		else if(protocol_response == ClientProtocolOK)
		{}
		else
			throw Indigo::Exception("Invalid protocol version response from server: " + toString(protocol_response));

		// Read assigned client avatar UID
		this->client_avatar_uid = readUIDFromStream(*socket);


		socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

		// Send CreateAvatar packet for this client's avatar
		SocketBufferOutStream packet;
		packet.writeUInt32(CreateAvatar);
		writeToStream(client_avatar_uid, packet);
		packet.writeStringLengthFirst(avatar_URL);
		writeToStream(Vec3d(0, 0, 0), packet); // pos 
		writeToStream(Vec3f(0, 0, 1), packet); // rotation

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
						const Vec3f rotation = readVec3FromStream<float>(*socket);

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->pos = pos;
								avatar->rotation = rotation;
								avatar->transform_dirty = true;

								//conPrint("updated avatar transform");

								avatar->pos_snapshots      [Maths::intMod(avatar->next_snapshot_i, Avatar::HISTORY_BUF_SIZE)] = pos;
								avatar->rotation_snapshots [Maths::intMod(avatar->next_snapshot_i, Avatar::HISTORY_BUF_SIZE)] = rotation;
								avatar->snapshot_times     [Maths::intMod(avatar->next_snapshot_i, Avatar::HISTORY_BUF_SIZE)] = Clock::getTimeSinceInit();
								//avatar->last_snapshot_time = Clock::getCurTimeRealSec();
								avatar->next_snapshot_i++;
							}
						}
						break;
					}
				case AvatarFullUpdate:
					{
						conPrint("AvatarFullUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								readFromNetworkStreamGivenUID(*socket, *avatar);
								avatar->other_dirty = true;
							}
							else
							{
								Avatar dummy;
								readFromNetworkStreamGivenUID(*socket, dummy);
							}
						}
						break;
					}
				case AvatarCreated:
					{
						conPrint("AvatarCreated");
						const UID avatar_uid = readUIDFromStream(*socket);
						const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f rotation = readVec3FromStream<float>(*socket);

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
								avatar->rotation = rotation;
								avatar->state = Avatar::State_JustCreated;
								avatar->other_dirty = true;
								world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

								avatar->setTransformAndHistory(pos, rotation);

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
								avatar->other_dirty = true;
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
									
									ob->pos_snapshots  [Maths::intMod(ob->next_snapshot_i, WorldObject::HISTORY_BUF_SIZE)] = pos;
									ob->axis_snapshots [Maths::intMod(ob->next_snapshot_i, WorldObject::HISTORY_BUF_SIZE)] = axis;
									ob->angle_snapshots[Maths::intMod(ob->next_snapshot_i, WorldObject::HISTORY_BUF_SIZE)] = angle;
									ob->snapshot_times[Maths::intMod(ob->next_snapshot_i, WorldObject::HISTORY_BUF_SIZE)] = Clock::getTimeSinceInit();
									//ob->last_snapshot_time = Clock::getCurTimeRealSec();
									ob->next_snapshot_i++;

									ob->from_remote_transform_dirty = true;

									//conPrint("updated object transform");
								}
							}
						}
						break;
					}
				case ObjectFullUpdate:
					{
						conPrint("ObjectFullUpdate");
						const UID object_uid = readUIDFromStream(*socket);

						// Look up existing object in world state
						{
							bool read = false;
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								if(main_window->selected_ob.getPointer() != ob) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
								{
									readFromNetworkStreamGivenUID(*socket, *ob);
									read = true;
									ob->from_remote_other_dirty = true;
								}
							}

							// Make sure we have read the whole object from the network stream
							if(!read)
							{
								WorldObject dummy;
								readFromNetworkStreamGivenUID(*socket, dummy);
							}

						}
						break;
					}
				case ObjectCreated:
					{
						conPrint("ObjectCreated");
						const UID object_uid = readUIDFromStream(*socket);
						//const std::string name = socket->readStringLengthFirst(); //TODO: enforce max len
						//const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						//const Vec3d pos = readVec3FromStream<double>(*socket);
						//const Vec3f axis = readVec3FromStream<float>(*socket);
						//const float angle = socket->readFloat();

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
								readFromNetworkStreamGivenUID(*socket, *ob);
								//ob->model_url = model_url;
								//ob->pos = pos;
								//ob->axis = axis;
								//ob->angle = angle;
								ob->state = WorldObject::State_JustCreated;
								ob->from_remote_other_dirty = true;
								world_state->objects.insert(std::make_pair(object_uid, ob));

								ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);

								conPrint("created new object");
							}
							else
							{
								WorldObject dummy_object;
								readFromNetworkStreamGivenUID(*socket, dummy_object);
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
								ob->from_remote_other_dirty = true;
							}
						}
						break;
					}
				case GetFile:
					{
						conPrint("Received GetFile message from server.");
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						conPrint("model_url: '" + model_url + "'");

						out_msg_queue->enqueue(new GetFileMessage(model_url));
						break;
					}
				case NewResourceOnServer:
					{
						conPrint("Received NewResourceOnServer message from server.");
						const std::string url = socket->readStringLengthFirst(MAX_STRING_LEN);
						conPrint("url: '" + url + "'");

						out_msg_queue->enqueue(new NewResourceOnServerMessage(url));
						break;
					}
				case ChatMessageID:
					{
						conPrint("ChatMessage");
						const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new ChatMessage(name, msg));
						break;
					}
				case UserSelectedObject:
					{
						//conPrint("Received UserSelectedObject msg.");
						const UID avatar_uid = readUIDFromStream(*socket);
						const UID object_uid = readUIDFromStream(*socket);
						out_msg_queue->enqueue(new UserSelectedObjectMessage(avatar_uid, object_uid));
						break;
					}
				case UserDeselectedObject:
					{
						//conPrint("Received UserDeselectedObject msg.");
						const UID avatar_uid = readUIDFromStream(*socket);
						const UID object_uid = readUIDFromStream(*socket);
						out_msg_queue->enqueue(new UserDeselectedObjectMessage(avatar_uid, object_uid));
						break;
					}
				case InfoMessageID:
					{
						//conPrint("Received InfoMessage msg.");
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new InfoMessage(msg));
						break;
					}
				case ErrorMessageID:
					{
						//conPrint("Received ErrorMessage msg.");
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new ErrorMessage(msg));
						break;
					}
				case LoggedInMessageID:
					{
						//conPrint("Received LoggedInMessageID msg.");
						const UserID logged_in_user_id = readUserIDFromStream(*socket); 
						const std::string logged_in_username = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new LoggedInMessage(logged_in_user_id, logged_in_username));
						break;
					}
				case LoggedOutMessageID:
					{
						//conPrint("Received LoggedOutMessageID msg.");
						out_msg_queue->enqueue(new LoggedOutMessage());
						break;
					}
				case SignedUpMessageID:
					{
						//conPrint("Received SignedUpMessageID msg.");
						const UserID user_id = readUserIDFromStream(*socket);
						const std::string signed_up_username = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new SignedUpMessage(user_id, signed_up_username));
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

	out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage());
}


void ClientThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("ClientThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}


void ClientThread::enqueueDataToSend(const SocketBufferOutStream& packet) // threadsafe
{
	if(packet.buf.size() > 0)
	{
		std::string packet_string(packet.buf.size(), '\0');
		std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

		data_to_send.enqueue(packet_string);
		event_fd.notify();
	}
}
