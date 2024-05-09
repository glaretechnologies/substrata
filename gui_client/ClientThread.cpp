/*=====================================================================
ClientThread.cpp
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ClientThread.h"


#include "ClientSenderThread.h"
#include "WorldState.h"
#include "../shared/Protocol.h"
#include "../shared/ProtocolStructs.h"
#include "../shared/Parcel.h"
#include <networking/MySocket.h>
#include <networking/TLSSocket.h>
#include <networking/Networking.h>
#include <maths/vec3.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/ConPrint.h>
#include <utils/Clock.h>
#include <utils/PoolAllocator.h>
#include <utils/Timer.h>
#if EMSCRIPTEN
#include <networking/EmscriptenWebSocket.h>
#endif


static const size_t MAX_STRING_LEN = 10000;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& hostname_, int port_,
						   const std::string& world_name_, struct tls_config* config_, const Reference<glare::PoolAllocator>& world_ob_pool_allocator_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	port(port_),
	world_name(world_name_),
	all_objects_received(false),
	config(config_),
	world_ob_pool_allocator(world_ob_pool_allocator_),
	send_data_to_socket(false)
{
#if !defined(EMSCRIPTEN)
	MySocketRef mysocket = new MySocket();
	mysocket->setUseNetworkByteOrder(false);
	socket = mysocket;
#endif
}


ClientThread::~ClientThread()
{}


void ClientThread::kill()
{
#if EMSCRIPTEN
	if(socket.nonNull())
		socket->startGracefulShutdown();
#endif

	client_sender_thread_manager.killThreadsNonBlocking();

	should_die = glare::atomic_int(1);
	
	event_fd.notify(); // Make the blocking readable call stop.
}


// This executes in the ClientThread context.
// We call ungracefulShutdown() on the socket.  This results in any current blocking call returning with WSAEINTR ('blocking operation was interrupted by a call to WSACancelBlockingCall')
#if defined(_WIN32)
static void asyncProcedure(uint64 data)
{
	ClientThread* client_thread = (ClientThread*)data;
	if(client_thread->socket.nonNull())
		client_thread->socket->ungracefulShutdown();

	client_thread->decRefCount();
}
#endif


void ClientThread::killConnection()
{
#if defined(_WIN32)
	this->incRefCount();
	QueueUserAPC(asyncProcedure, this->getHandle(), /*data=*/(ULONG_PTR)this);
#else
	if(socket.nonNull())
		socket->ungracefulShutdown();
#endif
}


WorldObjectRef ClientThread::allocWorldObject()
{
	glare::PoolAllocator::AllocResult alloc_res = this->world_ob_pool_allocator->alloc();

	WorldObject* ob_ptr = new (alloc_res.ptr) WorldObject(); // construct with placement new
	ob_ptr->allocator = this->world_ob_pool_allocator.ptr();
	ob_ptr->allocation_index = alloc_res.index;

	return ob_ptr;
}


void ClientThread::readAndHandleMessage(const uint32 peer_protocol_version)
{
	// Read msg type and length
	uint32 msg_type_and_len[2];
	socket->readData(msg_type_and_len, sizeof(uint32) * 2);
	const uint32 msg_type = msg_type_and_len[0];
	const uint32 msg_len = msg_type_and_len[1];
				
	// conPrint("ClientThread: Read message header: id: " + toString(msg_type) + ", len: " + toString(msg_len));

	if((msg_len < sizeof(uint32) * 2) || (msg_len > 1000000))
		throw glare::Exception("Invalid message size: " + toString(msg_len));

	// Read entire message
	msg_buffer.buf.resizeNoCopy(msg_len);
	msg_buffer.read_index = sizeof(uint32) * 2;

	socket->readData(msg_buffer.buf.data() + sizeof(uint32) * 2, msg_len - sizeof(uint32) * 2); // Read rest of message, store in msg_buffer.

	switch(msg_type)
	{
	case Protocol::AllObjectsSent:
		{
			conPrint("All objects finished sending.");
			// This message has no payload.
			this->all_objects_received = true;
			break;
		}
	case Protocol::AudioStreamToServerStarted:
		{
			// conPrint("ClientThread: received Protocol::AudioStreamToServerStarted");

			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const uint32 sampling_rate = msg_buffer.readUInt32();
			const uint32 flags = msg_buffer.readUInt32();
			const uint32 stream_id = msg_buffer.readUInt32();

			out_msg_queue->enqueue(new RemoteClientAudioStreamToServerStarted(avatar_uid, sampling_rate, flags, stream_id)); // Inform MainWindow

			break;
		}
	case Protocol::AudioStreamToServerEnded:
		{
			conPrint("ClientThread: received Protocol::AudioStreamToServerEnded");

			const UID avatar_uid = readUIDFromStream(msg_buffer);

			out_msg_queue->enqueue(new RemoteClientAudioStreamToServerEnded(avatar_uid)); // Inform MainWindow

			break;
		}
	case Protocol::AvatarTransformUpdate:
		{
			//conPrint("AvatarTransformUpdate");
			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const Vec3d pos = readVec3FromStream<double>(msg_buffer);
			const Vec3f rotation = readVec3FromStream<float>(msg_buffer);
			const uint32 anim_state_and_input_bitflags = msg_buffer.readUInt32();

			// Look up existing avatar in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res != world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();
					avatar->pos = pos;
					avatar->rotation = rotation;
					avatar->anim_state = anim_state_and_input_bitflags & 0xFF;
					avatar->last_physics_input_bitflags = anim_state_and_input_bitflags >> 16;
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
			conPrint("received Protocol::AvatarFullUpdate");

			const UID avatar_uid = readUIDFromStream(msg_buffer);

			Avatar temp_avatar;
			readAvatarFromNetworkStreamGivenUID(msg_buffer, temp_avatar); // Read message data before grabbing lock

			// Look up existing avatar in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res != world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();
					avatar->copyNetworkStateFrom(temp_avatar);
					avatar->generatePseudoRandomNameColour();
					avatar->other_dirty = true;
				}
			}
			break;
		}
	case Protocol::AvatarIsHere:
		{
			conPrint("received Protocol::AvatarIsHere");

			const UID avatar_uid = readUIDFromStream(msg_buffer);
			Avatar temp_avatar;
			readAvatarFromNetworkStreamGivenUID(msg_buffer, temp_avatar); // Read message data before grabbing lock

			// Look up existing avatar in world state
			{
				::Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res == world_state->avatars.end())
				{
					// Avatar for UID not already created, create it now.
					AvatarRef avatar = new Avatar();
					avatar->uid = avatar_uid;
					avatar->our_avatar = this->client_avatar_uid == avatar_uid;
					avatar->copyNetworkStateFrom(temp_avatar);
					avatar->state = Avatar::State_JustCreated;
					avatar->other_dirty = true;
					avatar->generatePseudoRandomNameColour();
					world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

					avatar->setTransformAndHistory(temp_avatar.pos, temp_avatar.rotation);

					out_msg_queue->enqueue(new AvatarIsHereMessage(avatar_uid)); // Inform MainWindow
				}
			}
			break;
		}
	case Protocol::AvatarCreated:
		{
			conPrint("received Protocol::AvatarCreated");

			const UID avatar_uid = readUIDFromStream(msg_buffer);
			Avatar temp_avatar;
			readAvatarFromNetworkStreamGivenUID(msg_buffer, temp_avatar); // Read message data before grabbing lock

			// Look up existing avatar in world state
			{
				::Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res == world_state->avatars.end())
				{
					// Avatar for UID not already created, create it now.
					AvatarRef avatar = new Avatar();
					avatar->uid = avatar_uid;
					avatar->our_avatar = this->client_avatar_uid == avatar_uid;
					avatar->copyNetworkStateFrom(temp_avatar);
					avatar->state = Avatar::State_JustCreated;
					avatar->other_dirty = true;
					avatar->generatePseudoRandomNameColour();
					world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

					avatar->setTransformAndHistory(temp_avatar.pos, temp_avatar.rotation);

					out_msg_queue->enqueue(new AvatarCreatedMessage(avatar_uid)); // Inform MainWindow
				}
			}
			break;
		}
	case Protocol::AvatarDestroyed:
		{
			conPrint("AvatarDestroyed");
			const UID avatar_uid = readUIDFromStream(msg_buffer);

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
	case Protocol::AvatarPerformGesture:
		{
			//conPrint("AvatarPerformGesture");
			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const std::string gesture_name = msg_buffer.readStringLengthFirst(10000);

			//conPrint("Received AvatarPerformGesture: '" + gesture_name + "'");

			out_msg_queue->enqueue(new AvatarPerformGestureMessage(avatar_uid, gesture_name));

			break;
		}
	case Protocol::AvatarStopGesture:
		{
			//conPrint("AvatarStopGesture");
			const UID avatar_uid = readUIDFromStream(msg_buffer);

			out_msg_queue->enqueue(new AvatarStopGestureMessage(avatar_uid));

			break;
		}
	case Protocol::AvatarEnteredVehicle:
		{
			// conPrint("AvatarEnteredVehicle");

			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const UID vehicle_ob_uid = readUIDFromStream(msg_buffer);
			const uint32 seat_index = msg_buffer.readUInt32();
			/*const uint32 flags =*/ msg_buffer.readUInt32();

			if(avatar_uid != this->client_avatar_uid) // Discard AvatarEnteredVehicle messages we sent. 
			{
				Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res != world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();

					auto res2 = world_state->objects.find(vehicle_ob_uid);
					if(res2 != world_state->objects.end())
					{
						if(avatar->entered_vehicle != res2.getValue()) // If this avatar is not already in the vehicle (AvatarEnteredVehicle messages are sent repeatedly)
						{
							avatar->entered_vehicle = res2.getValue();
							avatar->vehicle_seat_index = seat_index;
							avatar->pending_vehicle_transition = Avatar::EnterVehicle;
						}
					}
				}
			}

			break;
		}
	case Protocol::AvatarExitedVehicle:
		{
			conPrint("AvatarExitedVehicle");

			const UID avatar_uid = readUIDFromStream(msg_buffer);

			if(avatar_uid != this->client_avatar_uid) // Discard AvatarExitedVehicle messages we sent. 
			{
				Lock lock(world_state->mutex);
				auto res = world_state->avatars.find(avatar_uid);
				if(res != world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();
					avatar->pending_vehicle_transition = Avatar::ExitVehicle;
				}
			}

			break;
		}
	case Protocol::ObjectTransformUpdate:
		{
			//conPrint("ObjectTransformUpdate");
			const UID object_uid = readUIDFromStream(msg_buffer);
			const Vec3d pos = readVec3FromStream<double>(msg_buffer);
			const Vec3f axis = readVec3FromStream<float>(msg_buffer);
			const float angle = msg_buffer.readFloat();
			const Vec3f scale = readVec3FromStream<float>(msg_buffer);

			// Read transform_update_avatar_uid, added during protocol version 36.
			uint32 transform_update_avatar_uid = std::numeric_limits<uint32>::max();
			if(!msg_buffer.endOfStream())
				transform_update_avatar_uid = msg_buffer.readUInt32();

			// conPrint("ClientThread: received ObjectTransformUpdate, transform_update_avatar_uid: " + toString(transform_update_avatar_uid));

			if(transform_update_avatar_uid != (uint32)this->client_avatar_uid.value()) // Discard ObjectTransformUpdate messages we sent. 
			{
				// Look up existing object in world state
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
								
					WorldObject* ob = res.getValue().ptr();
#if GUI_CLIENT
					if(!ob->is_selected) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
#endif
					{
						//conPrint("ObjectTransformUpdate: setting ob pos to " + pos.toString());
#if GUI_CLIENT
						//ob->last_pos = ob->pos;
#endif
						ob->pos = pos;
						ob->axis = axis;
						ob->angle = angle;
						ob->scale = scale;

						// If we had physics snapshots, reset snapshots.
						if(ob->snapshots_are_physics_snapshots)
						{
							// conPrint("Resetting snapshots.");
							ob->next_insertable_snapshot_i = 0;
							ob->next_snapshot_i = 0;
						}
						ob->snapshots_are_physics_snapshots = false;

									
						ob->snapshots[ob->next_snapshot_i % (uint32)WorldObject::HISTORY_BUF_SIZE] = 
							WorldObject::Snapshot({pos.toVec4fPoint(), Quatf::fromAxisAndAngle(normalise(axis), angle), /*linear vel=*/Vec4f(0.f), /*angular_vel=*/Vec4f(0.f), /*client time=*/0.0, /*local time=*/Clock::getTimeSinceInit()});

						ob->next_snapshot_i++;

						ob->from_remote_transform_dirty = true;
						world_state->dirty_from_remote_objects.insert(ob);

						//conPrint("updated object transform");
					}
				}
			}
			else
			{
				// conPrint("\tDiscarding ObjectTransformUpdate message, as we sent it.");
			}
			break;
		}
		case Protocol::SummonObject:
		{
			// conPrint("ClientThread: received SummonObject message.");

			SummonObjectMessageServerToClient summon_msg;
			msg_buffer.readData(&summon_msg, sizeof(SummonObjectMessageServerToClient));

			if(summon_msg.transform_update_avatar_uid != (uint32)this->client_avatar_uid.value()) // Discard SummonObject messages we sent. 
			{
				// Look up existing object in world state
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(summon_msg.object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();
#if GUI_CLIENT
					if(!ob->is_selected) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
#endif
					{
						ob->setTransformAndHistory(summon_msg.pos, summon_msg.axis, summon_msg.angle);
						ob->transformChanged();

						ob->next_insertable_snapshot_i = 0;
						ob->next_snapshot_i = 0;
						ob->snapshots_are_physics_snapshots = false;

						ob->from_remote_summoned_dirty = true;
						world_state->dirty_from_remote_objects.insert(ob);
					}
				}
			}
			break;
		}
	case Protocol::ObjectPhysicsTransformUpdate:
		{
			const UID object_uid = readUIDFromStream(msg_buffer);
			const Vec3d pos = readVec3FromStream<double>(msg_buffer);

			Quatf rot;
			msg_buffer.readData(rot.v.x, sizeof(float) * 4);

			Vec4f linear_vel(0.f);
			msg_buffer.readData(linear_vel.x, sizeof(float) * 3);

			Vec4f angular_vel(0.f);
			msg_buffer.readData(angular_vel.x, sizeof(float) * 3);

			const uint32 transform_update_avatar_uid = msg_buffer.readUInt32();
			const double transform_client_time = msg_buffer.readDouble();

			//conPrint("ClientThread: received ObjectPhysicsTransformUpdate, transform_update_avatar_uid: " + toString(transform_update_avatar_uid));
			//conPrint("transform_client_time: " + toString(transform_client_time) + ", cur global time: " + toString(world_state->getCurrentGlobalTime()));

			if(transform_update_avatar_uid != (uint32)this->client_avatar_uid.value()) // Discard ObjectPhysicsTransformUpdate messages we sent.
			{
				// Look up existing object in world state
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					if(ob->physics_owner_id == transform_update_avatar_uid) // Only process messages that are from the physics owner of this object, discard others.
					{
						// If we had non-physics snapshots, reset snapshots.
						if(!ob->snapshots_are_physics_snapshots)
						{
							// conPrint("Resetting snapshots.");
							ob->next_insertable_snapshot_i = 0;
							ob->next_snapshot_i = 0;
						}
						ob->snapshots_are_physics_snapshots = true;

						const double local_time = Clock::getTimeSinceInit();

						ob->snapshots[ob->next_snapshot_i % (uint32)WorldObject::HISTORY_BUF_SIZE] = WorldObject::Snapshot({pos.toVec4fPoint(), rot, linear_vel, angular_vel, transform_client_time, local_time});

						ob->next_snapshot_i++;

						// conPrint("ClientThread: Added snapshot " + toString(ob->next_snapshot_i));

						//NEW: Compute transmission_time_offset: An estimate of local_clock_time - sending_clock_time.
						// TODO: Handle a different client taking over sending messages.
						/*if(ob->transmission_time_offset == std::numeric_limits<double>::infinity())
						{
							ob->transmission_time_offset = Clock::getTimeSinceInit() - last_transform_client_time;

							conPrint("Storing new ob->transmission_time_offset: " + doubleToString(ob->transmission_time_offset));
						}*/

						ob->from_remote_physics_transform_dirty = true;
						world_state->dirty_from_remote_objects.insert(ob);
					}
					else
					{
						// conPrint("\tDiscarding ObjectPhysicsTransformUpdate message as not from physics owner of object.");
					}
				}
			}
			else
			{
				// conPrint("\tDiscarding ObjectPhysicsTransformUpdate message as we sent it.");
			}

			break;
		}
	case Protocol::ObjectFullUpdate:
		{
			//conPrint("ObjectFullUpdate");
			const UID object_uid = readUIDFromStream(msg_buffer);

			// Look up existing object in world state
			{
				bool read = false;
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();
#if GUI_CLIENT
					if(!ob->is_selected) // Don't update the selected object - we will consider the local client control authoritative while the object is selected.
#endif
					{
						readWorldObjectFromNetworkStreamGivenUID(msg_buffer, *ob);
						read = true;
						ob->from_remote_other_dirty = true;
						world_state->dirty_from_remote_objects.insert(ob);
					}
				}

				// Make sure we have read the whole object from the network stream
				if(!read)
				{
					WorldObject dummy;
					readWorldObjectFromNetworkStreamGivenUID(msg_buffer, dummy);
				}

			}
			break;
		}
	case Protocol::ObjectLightmapURLChanged:
		{
			//conPrint("ObjectLightmapURLChanged");
			const UID object_uid = readUIDFromStream(msg_buffer);
			const std::string new_lightmap_url = msg_buffer.readStringLengthFirst(10000);
			//conPrint("new_lightmap_url: " + new_lightmap_url);

			// Look up existing object in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

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
			const UID object_uid = readUIDFromStream(msg_buffer);
			const std::string new_model_url = msg_buffer.readStringLengthFirst(10000);
			//conPrint("new_model_url: " + new_model_url);

			// Look up existing object in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					ob->model_url = new_model_url;

					ob->from_remote_model_url_dirty = true;
					world_state->dirty_from_remote_objects.insert(ob);
				}
			}
			break;
		}
	case Protocol::ObjectFlagsChanged:
		{
			const UID object_uid = readUIDFromStream(msg_buffer);
			const uint32 flags = msg_buffer.readUInt32();
			//conPrint("ObjectFlagsChanged: read flags " + toString(flags) + " for ob with UID " + object_uid.toString());

			// Look up existing object in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();
								
					ob->flags = flags; // Copy flags
					ob->from_remote_other_dirty = true;
					world_state->dirty_from_remote_objects.insert(ob);
				}
			}
			break;
		}
	case Protocol::ObjectPhysicsOwnershipTaken:
		{
			const UID object_uid = readUIDFromStream(msg_buffer);
			const uint32 physics_owner_id = msg_buffer.readUInt32();
			const double last_physics_ownership_change_global_time = msg_buffer.readDouble();
			const uint32 flags = msg_buffer.readUInt32();

			const bool renewal = BitUtils::isBitSet(flags, 0x1u); // See if flag bit is set which indicated this message is renewing ownership.

			// conPrint("ClientThread: Received ObjectPhysicsOwnershipTaken, object_uid: " + object_uid.toString() + ", physics_owner_id: " + toString(physics_owner_id) + ", last_physics_ownership_change_global_time: " + 
			// 	toString(last_physics_ownership_change_global_time) + ", renewal: " + boolToString(renewal));

			// Look up existing object in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					if(last_physics_ownership_change_global_time > ob->last_physics_ownership_change_global_time)
					{
						ob->physics_owner_id = physics_owner_id;
						ob->last_physics_ownership_change_global_time = last_physics_ownership_change_global_time;

						ob->from_remote_physics_ownership_dirty = true;
						world_state->dirty_from_remote_objects.insert(ob);

						if(renewal)
						{
							if(ob->transmission_time_offset == 0) // If we haven't computed transmission_time_offset yet, because we received info about the object after another client became physics owner of it 
								// (e.g. we just connected to server)
								ob->transmission_time_offset = world_state->getCurrentGlobalTime() - last_physics_ownership_change_global_time; // Compute it.
						}
						else
						{
							ob->transmission_time_offset = world_state->getCurrentGlobalTime() - last_physics_ownership_change_global_time;

							// conPrint("Storing new ob->transmission_time_offset: " + doubleToString(ob->transmission_time_offset));

							ob->next_insertable_snapshot_i = ob->next_snapshot_i; // Effectively remove queued snapshots.
						}
					}
				}
			}
			break;
		}
	case Protocol::ObjectCreated:
		{
			//conPrint("ObjectCreated");
			const UID object_uid = readUIDFromStream(msg_buffer);

			// Read from network
			WorldObjectRef ob = allocWorldObject();
			ob->uid = object_uid;
			readWorldObjectFromNetworkStreamGivenUID(msg_buffer, *ob);

			ob->state = WorldObject::State_JustCreated;
			ob->from_remote_other_dirty = true;
			ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);

			// Insert into world state.
			{
				::Lock lock(world_state->mutex);

				// NOTE: will not replace existing object with that UID if it exists in the map.
				const bool added = world_state->objects.insert(object_uid, ob);
				if(added)
					world_state->dirty_from_remote_objects.insert(ob);
			}
			break;
		}
	case Protocol::ObjectInitialSend:
		{
			// NOTE: currently same code/semantics as ObjectCreated
			//conPrint("ObjectInitialSend");
			const UID object_uid = readUIDFromStream(msg_buffer);

			// Read from network
			WorldObjectRef ob = allocWorldObject();
			ob->uid = object_uid;
			readWorldObjectFromNetworkStreamGivenUID(msg_buffer, *ob);

			if(!isFinite(ob->angle))
				ob->angle = 0;
			if(!ob->axis.isFinite())
				ob->axis = Vec3f(1,0,0);

			ob->state = WorldObject::State_InitialSend;
			ob->from_remote_other_dirty = true;
			ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);

			// TEMP HACK: set a smaller max loading distance for CV features
			const char* feature_prefix = "CryptoVoxels Feature, uuid: ";
			if(hasPrefix(ob->content, feature_prefix))
				ob->max_load_dist2 = Maths::square(100.f);

			// Insert into world state.
			{
				::Lock lock(world_state->mutex);

				// When a client moves and a new cell comes into proximity, a QueryObjects message is sent to the server.
				// The server replies with ObjectInitialSend messages.
				// This means that the client may already have the object inserted, when moving back into a cell previously in proximity.
				// We want to make sure not to add the object twice or load it into the graphics engine twice.
				const bool added = world_state->objects.insert(object_uid, ob);
				if(added)
					world_state->dirty_from_remote_objects.insert(ob);
			}
			break;
		}
	case Protocol::ObjectDestroyed:
		{
			conPrint("ObjectDestroyed");
			const UID object_uid = readUIDFromStream(msg_buffer);

			// Mark object as dead
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();
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
			const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);
			readFromNetworkStreamGivenID(msg_buffer, *parcel, peer_protocol_version);
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
			const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);

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
			const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);

			// Look up existing parcel in world state
			{
				bool read = false;
				Lock lock(world_state->mutex);
				auto res = world_state->parcels.find(parcel_id);
				if(res != world_state->parcels.end())
				{
					Parcel* parcel = res->second.getPointer();
					readFromNetworkStreamGivenID(msg_buffer, *parcel, peer_protocol_version);
					read = true;
					parcel->from_remote_dirty = true;
					world_state->dirty_from_remote_parcels.insert(parcel);
				}

				// Make sure we have read the whole pracel from the network stream
				if(!read)
				{
					Parcel dummy;
					readFromNetworkStreamGivenID(msg_buffer, dummy, peer_protocol_version);
				}
			}
			break;
		}
	case Protocol::GetFile:
		{
			conPrint("Received GetFile message from server.");
			const std::string model_url = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			conPrint("model_url: '" + model_url + "'");

			out_msg_queue->enqueue(new GetFileMessage(model_url));
			break;
		}
	case Protocol::NewResourceOnServer:
		{
			//conPrint("Received NewResourceOnServer message from server.");
			const std::string url = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			//conPrint("url: '" + url + "'");

			out_msg_queue->enqueue(new NewResourceOnServerMessage(url));
			break;
		}
	case Protocol::ChatMessageID:
		{
			//conPrint("ChatMessage");
			const std::string name = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			const std::string msg = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			out_msg_queue->enqueue(new ChatMessage(name, msg));
			break;
		}
	case Protocol::UserSelectedObject:
		{
			//conPrint("Received UserSelectedObject msg.");
			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const UID object_uid = readUIDFromStream(msg_buffer);
			out_msg_queue->enqueue(new UserSelectedObjectMessage(avatar_uid, object_uid));
			break;
		}
	case Protocol::UserDeselectedObject:
		{
			//conPrint("Received UserDeselectedObject msg.");
			const UID avatar_uid = readUIDFromStream(msg_buffer);
			const UID object_uid = readUIDFromStream(msg_buffer);
			out_msg_queue->enqueue(new UserDeselectedObjectMessage(avatar_uid, object_uid));
			break;
		}
	case Protocol::InfoMessageID:
		{
			//conPrint("Received InfoMessage msg.");
			const std::string msg = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			out_msg_queue->enqueue(new InfoMessage(msg));
			break;
		}
	case Protocol::ErrorMessageID:
		{
			//conPrint("Received ErrorMessage msg.");
			const std::string msg = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			out_msg_queue->enqueue(new ErrorMessage(msg));
			break;
		}
	case Protocol::LoggedInMessageID:
		{
			conPrint("Received LoggedInMessageID msg.");
			const UserID logged_in_user_id = readUserIDFromStream(msg_buffer); 
			const std::string logged_in_username = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			Reference<LoggedInMessage> msg = new LoggedInMessage(logged_in_user_id, logged_in_username);
						
			readAvatarSettingsFromStream(msg_buffer, msg->avatar_settings);

			uint32 logged_in_user_flags = 0;
			if(msg_buffer.canReadNBytes(sizeof(uint32))) // Added sending flags after avatar settings
				logged_in_user_flags = msg_buffer.readUInt32();

			msg->user_flags = logged_in_user_flags;

			out_msg_queue->enqueue(msg);

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
			const UserID user_id = readUserIDFromStream(msg_buffer);
			const std::string signed_up_username = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			out_msg_queue->enqueue(new SignedUpMessage(user_id, signed_up_username));
			break;
		}
	case Protocol::TimeSyncMessage:
		{
			const double global_time = msg_buffer.readDouble();
			world_state->updateWithGlobalTimeReceived(global_time);
			break;
		}
	case Protocol::ServerAdminMessageID:
		{
			const std::string msg_text = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
			out_msg_queue->enqueue(new ServerAdminMessage(msg_text));
			break;
		}
	case Protocol::WorldSettingsInitialSendMessage:
		{
			Reference<WorldSettingsReceivedMessage> msg = new WorldSettingsReceivedMessage(/*is_initial_send=*/true);
			readWorldSettingsFromStream(msg_buffer, msg->world_settings); // Read from msg_buffer, write to msg->world_settings
			out_msg_queue->enqueue(msg);
			break;
		}
	case Protocol::WorldSettingsUpdate:
		{
			Reference<WorldSettingsReceivedMessage> msg = new WorldSettingsReceivedMessage(/*is_initial_send=*/false);
			readWorldSettingsFromStream(msg_buffer, msg->world_settings); // Read from msg_buffer, write to msg->world_settings
			out_msg_queue->enqueue(msg);
			break;
		}
	case Protocol::MapTilesResult:
		{
			Reference<MapTilesResultReceivedMessage> msg = new MapTilesResultReceivedMessage();
			const uint32 num_tiles = msg_buffer.readUInt32();
			if(num_tiles > 1000)
				throw glare::Exception("MapTilesResult: too many tiles: " + toString(num_tiles));

			// Read tile coords
			msg->tile_indices.resize(num_tiles);
			msg_buffer.readData(msg->tile_indices.data(), num_tiles * sizeof(Vec3i));

			// Read URLS
			msg->tile_URLS.resize(num_tiles);
			for(uint32 i=0; i<num_tiles; ++i)
				msg->tile_URLS[i] = msg_buffer.readStringLengthFirst(1000);

			out_msg_queue->enqueue(msg);
			break;
		}
	default:
		{
			conPrint("Unknown message id: " + ::toString(msg_type));
		}
	}
}


void ClientThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientThread");

	try
	{
		// Do DNS resolution of server hostname
		//Timer timer;
#if EMSCRIPTEN
		const IPAddress server_ip("0.0.0.0"); // TEMP HACK, not used in emscripten currently (used for UDP stuff)
#else
		const std::vector<IPAddress> server_ips = Networking::doDNSLookup(hostname);
		//conPrint("DNS resolution of '" + hostname + "' took " + timer.elapsedString());
		const IPAddress server_ip = server_ips[0];

		
#endif
		conPrint("ClientThread Connecting to " + hostname + ":" + toString(port) + "...");

		out_msg_queue->enqueue(new ClientConnectingToServerMessage(server_ip));


#if EMSCRIPTEN
		socket = new EmscriptenWebSocket();

		const bool use_TLS = hostname != "localhost"; // Don't use TLS on localhost for now, for testing.
		const std::string protocol = use_TLS ? "wss" : "ws";
		socket.downcast<EmscriptenWebSocket>()->connect(protocol, hostname, /*port=*/use_TLS ? 443 : 80);
#else
		socket.downcast<MySocket>()->connect(server_ip, hostname, port);

		socket = new TLSSocket(socket.downcast<MySocket>(), config, hostname);
#endif
		conPrint("ClientThread Connected to " + hostname + ":" + toString(port) + "!");

		// Do initial query-response part of protocol
		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeUpdates); // Write connection type

		socket->writeStringLengthFirst(world_name); // Write world name


		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw glare::Exception("Invalid hello from server: " + toString(hello_response));

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{		
			out_msg_queue->enqueue(new ClientProtocolTooOldMessage());

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

		// Read server protocol version
		const uint32 peer_protocol_version = socket->readUInt32();


		// Read assigned client avatar UID
		this->client_avatar_uid = readUIDFromStream(*socket);

		out_msg_queue->enqueue(new ClientConnectedToServerMessage(this->client_avatar_uid, peer_protocol_version));

#if defined(EMSCRIPTEN)
		js::Vector<uint8, 16> data_to_send_copy;
		{
			Lock lock(data_to_send_mutex);
			data_to_send_copy = data_to_send;
			data_to_send.clear();

			this->socket->writeData(data_to_send_copy.data(), data_to_send_copy.size());
			this->send_data_to_socket = true;
		}
#else
		
		// Now that we have finished the initial query-response part of protocol, start client_sender_thread, which will do the sending of data on the socket.
		// We do this on a separate thread to avoid deadlocks where both the client and server get stuck send()ing large amounts of data to each other, without doing any reads.
		Reference<ClientSenderThread> sender_thread = new ClientSenderThread(socket);
		{
			Lock lock(data_to_send_mutex);
			this->client_sender_thread = sender_thread;
		}
		client_sender_thread_manager.addThread(sender_thread);

		// Enqueue any data we have to send into the client_sender_thread queue.
		js::Vector<uint8, 16> data_to_send_copy;
		{
			Lock lock(data_to_send_mutex);
			data_to_send_copy = data_to_send;
			data_to_send.clear();
		}
		
		sender_thread->enqueueDataToSend(data_to_send_copy);
#endif


		socket->setNoDelayEnabled(true); // We will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.
		
		while(1)
		{
			if(should_die)
			{
				out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage());
				return;
			}

#if defined(_WIN32) || defined(OSX)

			// TODO: remove readable() check for win32, since we can interrupt socket calls properly and cleanly now.

			if(socket->readable(/*timeout (s)=*/0.1)) // If socket has some data to read from it:  (Use a timeout so we can check should_die occasionally)
			{
				readAndHandleMessage(peer_protocol_version);
			}

#elif defined(EMSCRIPTEN)
			
			readAndHandleMessage(peer_protocol_version);

#else // Else Linux:

			// Block until either the socket is readable or the event fd is signalled, which means should_die has been set.
			if(socket->readable(event_fd)) // If there is some data to read:
			{
				readAndHandleMessage(peer_protocol_version);
			}
#endif
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("ClientThread: Socket error: " + e.what());
		out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage(e.what(), /*closed_gracefully=*/e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully));
	}
	catch(glare::Exception& e)
	{
		conPrint("ClientThread: glare::Exception: " + e.what());
		out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage(e.what(), /*closed_gracefully=*/false));
	}

	out_msg_queue->enqueue(new ClientDisconnectedFromServerMessage());
}


void ClientThread::enqueueDataToSend(const ArrayRef<uint8> data)
{
#if defined(EMSCRIPTEN)
	Lock lock(data_to_send_mutex);

	if(socket.nonNull() && send_data_to_socket)
	{
		socket->writeData(data.data(), data.size());
		socket->flush();
	}
	else
	{
		// If socket has not been created yet, store in data_to_send until client_sender_thread is created.
		if(!data.empty())
		{
			const size_t write_i = data_to_send.size();
			data_to_send.resize(write_i + data.size());
			std::memcpy(&data_to_send[write_i], data.data(), data.size());
		}
	}

#else
	Lock lock(data_to_send_mutex);

	if(client_sender_thread.nonNull())
		client_sender_thread->enqueueDataToSend(data);
	else
	{
		// If client_sender_thread has not been created yet, store in data_to_send until client_sender_thread is created.
		if(!data.empty())
		{
			const size_t write_i = data_to_send.size();
			data_to_send.resize(write_i + data.size());
			std::memcpy(&data_to_send[write_i], data.data(), data.size());
		}
	}
#endif
}
