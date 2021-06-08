/*=====================================================================
ClientThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "ClientThread.h"


#include "WorldState.h"
#include <MySocket.h>
#include <TLSSocket.h>
#include "MainWindow.h"
#include "../shared/Protocol.h"
#include "../shared/Parcel.h"
#include <vec3.h>
#include <SocketBufferOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <ConPrint.h>


static const bool VERBOSE = false;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& hostname_, int port_,
						   const std::string& avatar_URL_, const std::string& world_name_, struct tls_config* config_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	port(port_),
	avatar_URL(avatar_URL_),
	world_name(world_name_),
	all_objects_received(false),
	config(config_)
{
	MySocketRef mysocket = new MySocket();
	mysocket->setUseNetworkByteOrder(false);
	socket = mysocket;
}


ClientThread::~ClientThread()
{}


void ClientThread::kill()
{
	should_die = glare::atomic_int(1);
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

		socket.downcast<MySocket>()->connect(hostname, port);

		socket = new TLSSocket(socket.downcast<MySocket>(), config, hostname);

		conPrint("ClientThread Connected to " + hostname + ":" + toString(port) + "!");

		out_msg_queue->enqueue(new ClientConnectedToServerMessage());

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeUpdates); // Write connection type

		socket->writeStringLengthFirst(world_name); // Write world name


		const int MAX_STRING_LEN = 10000;

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw glare::Exception("Invalid hello from server: " + toString(hello_response));

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw glare::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolTooNew)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw glare::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{}
		else
			throw glare::Exception("Invalid protocol version response from server: " + toString(protocol_response));

		// Read assigned client avatar UID
		this->client_avatar_uid = readUIDFromStream(*socket);


		socket->setNoDelayEnabled(true); // We will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.
		

		while(1) // write to / read from socket loop
		{
			if(should_die)
			{
				out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage());
				return;
			}

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
				case Protocol::AllObjectsSent:
					{
						conPrint("All objects finished sending.  " + toString(world_state->objects.size()) + " objects.");
						// This message has no payload.
						this->all_objects_received = true;
						break;
					}
				case Protocol::AvatarTransformUpdate:
					{
						//conPrint("AvatarTransformUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f rotation = readVec3FromStream<float>(*socket);
						const uint32 anim_state = socket->readUInt32();

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->pos = pos;
								avatar->rotation = rotation;
								avatar->anim_state = anim_state;
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
				case Protocol::AvatarFullUpdate:
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
								avatar->generatePseudoRandomNameColour();
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
				case Protocol::AvatarIsHere:
					{
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
								avatar->generatePseudoRandomNameColour();
								world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

								avatar->setTransformAndHistory(pos, rotation);

								out_msg_queue->enqueue(new AvatarIsHereMessage(avatar_uid)); // Inform MainWindow
							}
						}
						break;
					}
				case Protocol::AvatarCreated:
					{
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
								avatar->generatePseudoRandomNameColour();
								world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

								avatar->setTransformAndHistory(pos, rotation);

								out_msg_queue->enqueue(new AvatarCreatedMessage(avatar_uid)); // Inform MainWindow
							}
						}
						break;
					}
				case Protocol::AvatarDestroyed:
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
				case Protocol::ObjectTransformUpdate:
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
#if GUI_CLIENT
								if(!ob->is_selected) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
#endif
								{
									//conPrint("ObjectTransformUpdate: setting ob pos to " + pos.toString());
#if GUI_CLIENT
									ob->last_pos = ob->pos;
#endif
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
									world_state->dirty_from_remote_objects.insert(ob);

									//conPrint("updated object transform");
								}
							}
						}
						break;
					}
				case Protocol::ObjectFullUpdate:
					{
						//conPrint("ObjectFullUpdate");
						const UID object_uid = readUIDFromStream(*socket);

						// Look up existing object in world state
						{
							bool read = false;
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
#if GUI_CLIENT
								if(!ob->is_selected) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
#endif
								{
									readFromNetworkStreamGivenUID(*socket, *ob);
									read = true;
									ob->from_remote_other_dirty = true;
									world_state->dirty_from_remote_objects.insert(ob);
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
				case Protocol::ObjectLightmapURLChanged:
					{
						//conPrint("ObjectLightmapURLChanged");
						const UID object_uid = readUIDFromStream(*socket);
						const std::string new_lightmap_url = socket->readStringLengthFirst(10000);
						//conPrint("new_lightmap_url: " + new_lightmap_url);

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();

								ob->lightmap_url = new_lightmap_url;

								ob->from_remote_lightmap_url_dirty = true;
								world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::ObjectModelURLChanged:
					{
						//conPrint("ObjectModelURLChanged");
						const UID object_uid = readUIDFromStream(*socket);
						const std::string new_model_url = socket->readStringLengthFirst(10000);
						//conPrint("new_model_url: " + new_model_url);

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();

								ob->model_url = new_model_url;

								ob->from_remote_model_url_dirty = true;
								world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::ObjectFlagsChanged:
					{
						const UID object_uid = readUIDFromStream(*socket);
						const uint32 flags = socket->readUInt32();
						//conPrint("ObjectFlagsChanged: read flags " + toString(flags) + " for ob with UID " + object_uid.toString());

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								
								ob->flags = flags; // Copy flags
								ob->from_remote_other_dirty = true;
								world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::ObjectCreated:
					{
						//conPrint("ObjectCreated");
						const UID object_uid = readUIDFromStream(*socket);

						// Read from network
						WorldObjectRef ob = new WorldObject();
						ob->uid = object_uid;
						readFromNetworkStreamGivenUID(*socket, *ob);

						ob->state = WorldObject::State_JustCreated;
						ob->from_remote_other_dirty = true;
						ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);

						// Insert into world state.
						{
							::Lock lock(world_state->mutex);

							// NOTE: will not replace existing object with that UID if it exists in the map.
							world_state->objects.insert(std::make_pair(object_uid, ob));

							world_state->dirty_from_remote_objects.insert(ob.ptr());
						}
						break;
					}
				case Protocol::ObjectInitialSend:
					{
						// NOTE: currently same code/semantics as ObjectCreated
						//conPrint("ObjectInitialSend");
						const UID object_uid = readUIDFromStream(*socket);

						// Read from network
						WorldObjectRef ob = new WorldObject();
						ob->uid = object_uid;
						readFromNetworkStreamGivenUID(*socket, *ob);

						if(!isFinite(ob->angle))
							ob->angle = 0;

						ob->state = WorldObject::State_JustCreated;
						ob->from_remote_other_dirty = true;
						ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);

						// TEMP HACK: set a smaller max loading distance for CV features
						const std::string feature_prefix = "CryptoVoxels Feature, uuid: ";
						if(hasPrefix(ob->content, feature_prefix))
							ob->max_load_dist2 = Maths::square(100.f);

						// Insert into world state.
						{
							::Lock lock(world_state->mutex);

							// NOTE: will not replace existing object with that UID if it exists in the map.
							world_state->objects.insert(std::make_pair(object_uid, ob));

							world_state->dirty_from_remote_objects.insert(ob.ptr());
						}
						break;
					}
				case Protocol::ObjectDestroyed:
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
								world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::ParcelCreated:
					{
						ParcelRef parcel = new Parcel();
						const ParcelID parcel_id = readParcelIDFromStream(*socket);
						readFromNetworkStreamGivenID(*socket, *parcel);
						parcel->id = parcel_id;
						parcel->state = Parcel::State_JustCreated;
						parcel->from_remote_dirty = true;

						{
							::Lock lock(world_state->mutex);
							world_state->parcels[parcel->id] = parcel;
							world_state->dirty_from_remote_parcels.insert(parcel);
						}
						break;
					}
				case Protocol::ParcelDestroyed:
					{
						conPrint("ParcelDestroyed");
						const ParcelID parcel_id = readParcelIDFromStream(*socket);

						// Mark parcel as dead
						{
							Lock lock(world_state->mutex);
							auto res = world_state->parcels.find(parcel_id);
							if(res != world_state->parcels.end())
							{
								Parcel* parcel = res->second.getPointer();
								parcel->state = Parcel::State_Dead;
								parcel->from_remote_dirty = true;
								world_state->dirty_from_remote_parcels.insert(parcel);
							}
						}
						break;
					}
				case Protocol::ParcelFullUpdate:
					{
						conPrint("ParcelFullUpdate");
						const ParcelID parcel_id = readParcelIDFromStream(*socket);

						// Look up existing parcel in world state
						{
							bool read = false;
							Lock lock(world_state->mutex);
							auto res = world_state->parcels.find(parcel_id);
							if(res != world_state->parcels.end())
							{
								Parcel* parcel = res->second.getPointer();
								readFromNetworkStreamGivenID(*socket, *parcel);
								read = true;
								parcel->from_remote_dirty = true;
								world_state->dirty_from_remote_parcels.insert(parcel);
							}

							// Make sure we have read the whole pracel from the network stream
							if(!read)
							{
								Parcel dummy;
								readFromNetworkStreamGivenID(*socket, dummy);
							}
						}
						break;
					}
				case Protocol::GetFile:
					{
						conPrint("Received GetFile message from server.");
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						conPrint("model_url: '" + model_url + "'");

						out_msg_queue->enqueue(new GetFileMessage(model_url));
						break;
					}
				case Protocol::NewResourceOnServer:
					{
						//conPrint("Received NewResourceOnServer message from server.");
						const std::string url = socket->readStringLengthFirst(MAX_STRING_LEN);
						//conPrint("url: '" + url + "'");

						out_msg_queue->enqueue(new NewResourceOnServerMessage(url));
						break;
					}
				case Protocol::ChatMessageID:
					{
						conPrint("ChatMessage");
						const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new ChatMessage(name, msg));
						break;
					}
				case Protocol::UserSelectedObject:
					{
						//conPrint("Received UserSelectedObject msg.");
						const UID avatar_uid = readUIDFromStream(*socket);
						const UID object_uid = readUIDFromStream(*socket);
						out_msg_queue->enqueue(new UserSelectedObjectMessage(avatar_uid, object_uid));
						break;
					}
				case Protocol::UserDeselectedObject:
					{
						//conPrint("Received UserDeselectedObject msg.");
						const UID avatar_uid = readUIDFromStream(*socket);
						const UID object_uid = readUIDFromStream(*socket);
						out_msg_queue->enqueue(new UserDeselectedObjectMessage(avatar_uid, object_uid));
						break;
					}
				case Protocol::InfoMessageID:
					{
						//conPrint("Received InfoMessage msg.");
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new InfoMessage(msg));
						break;
					}
				case Protocol::ErrorMessageID:
					{
						//conPrint("Received ErrorMessage msg.");
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new ErrorMessage(msg));
						break;
					}
				case Protocol::LoggedInMessageID:
					{
						//conPrint("Received LoggedInMessageID msg.");
						const UserID logged_in_user_id = readUserIDFromStream(*socket); 
						const std::string logged_in_username = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new LoggedInMessage(logged_in_user_id, logged_in_username));
						break;
					}
				case Protocol::LoggedOutMessageID:
					{
						//conPrint("Received LoggedOutMessageID msg.");
						out_msg_queue->enqueue(new LoggedOutMessage());
						break;
					}
				case Protocol::SignedUpMessageID:
					{
						//conPrint("Received SignedUpMessageID msg.");
						const UserID user_id = readUserIDFromStream(*socket);
						const std::string signed_up_username = socket->readStringLengthFirst(MAX_STRING_LEN);
						out_msg_queue->enqueue(new SignedUpMessage(user_id, signed_up_username));
						break;
					}
				case Protocol::TimeSyncMessage:
					{
						const double global_time = socket->readDouble();
						world_state->updateWithGlobalTimeReceived(global_time);
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
				if(VERBOSE) conPrint("WorkerThread: event FD was signalled.");

				// The event FD was signalled, which means there is some data to send on the socket.
				// Reset the event fd by reading from it.
				event_fd.read();

				if(VERBOSE) conPrint("WorkerThread: event FD has been reset.");
#endif
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("ClientThread: Socket error: " + e.what());
		out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage(e.what()));
	}
	catch(glare::Exception& e)
	{
		conPrint("ClientThread: glare::Exception: " + e.what());
		out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage(e.what()));
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
