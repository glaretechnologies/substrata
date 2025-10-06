/*=====================================================================
ClientThread.cpp
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ClientThread.h"


#include "ClientSenderThread.h"
#include "WorldState.h"
#include "ThreadMessages.h"
#include "../shared/Protocol.h"
#include "../shared/ProtocolStructs.h"
#include "../shared/Parcel.h"
#include "../shared/WorldDetails.h"
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
#include <utils/FastPoolAllocator.h>
#include <utils/Timer.h>
#include <utils/BufferViewInStream.h>
#include <zstd.h>
#if EMSCRIPTEN
#include <networking/EmscriptenWebSocket.h>
#endif
#include <tracy/Tracy.hpp>


static const size_t MAX_STRING_LEN = 10000;


ClientThread::ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, const std::string& hostname_, int port_,
						   const std::string& world_name_, struct tls_config* config_, const Reference<glare::FastPoolAllocator>& world_ob_pool_allocator_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	port(port_),
	world_name(world_name_),
	all_objects_received(false),
	config(config_),
	world_ob_pool_allocator(world_ob_pool_allocator_),
	send_data_to_socket(false),
	dstream(nullptr)
{
#if !defined(EMSCRIPTEN)
	MySocketRef mysocket = new MySocket();
	mysocket->setUseNetworkByteOrder(false);
	socket = mysocket;
#endif

	dstream = ZSTD_createDStream();
	if(!dstream)
		throw glare::Exception("ZSTD_createDStream failed.");
}


ClientThread::~ClientThread()
{
	if(dstream)
		ZSTD_freeDStream(dstream);
}


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
	glare::FastPoolAllocator::AllocResult alloc_res = this->world_ob_pool_allocator->alloc();

	WorldObject* ob_ptr = new (alloc_res.ptr) WorldObject(); // construct with placement new
	ob_ptr->allocator = this->world_ob_pool_allocator.ptr();
	ob_ptr->allocation_index = alloc_res.index;

	return ob_ptr;
}


static void decompressWithZstdTo(const void* src, size_t src_len, glare::AllocatorVector<unsigned char, 16>& decompressed_buf_out)
{
	const uint64 decompressed_size = ZSTD_getFrameContentSize(src, src_len);
	if(decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
		throw glare::Exception("Failed to get decompressed_size");

	decompressed_buf_out.resizeNoCopy(decompressed_size);

	const size_t res = ZSTD_decompress(/*dest=*/decompressed_buf_out.data(), /*dest capacity =*/decompressed_buf_out.size(), /*source=*/src, /*compressed size=*/src_len);
	if(ZSTD_isError(res))
		throw glare::Exception("Decompression of buffer failed: " + std::string(ZSTD_getErrorName(res)));
	if(res < decompressed_size)
		throw glare::Exception("Decompression of buffer failed: not enough bytes in result");
}


void ClientThread::handleObjectInitialSend(RandomAccessInStream& msg_stream)
{
	// NOTE: currently same code/semantics as ObjectCreated
	//conPrint("ObjectInitialSend");
	const UID object_uid = readUIDFromStream(msg_stream);

	// Read from stream
	WorldObjectRef ob = allocWorldObject();
	ob->uid = object_uid;
	readWorldObjectFromNetworkStreamGivenUID(msg_stream, *ob);

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
}


void ClientThread::readAndHandleMessage(const uint32 peer_protocol_version)
{
	ZoneScopedN("ClientThread::readAndHandleMessage"); // Tracy profiler

	// Read msg type and length
	uint32 msg_type_and_len[2];
	const size_t msg_header_size_B = sizeof(uint32) * 2;
	socket->readData(msg_type_and_len, msg_header_size_B);
	const uint32 msg_type = msg_type_and_len[0];
	const uint32 msg_len  = msg_type_and_len[1]; // Total message length, including the message header.
				
	// conPrint("ClientThread: Read message header: id: " + toString(msg_type) + ", len: " + toString(msg_len));
#ifdef TRACY_ENABLE
	const std::string zone_text = "msg type: " + toString(msg_type) + ", len: " + toString(msg_len) + " B";
	ZoneText(zone_text.c_str(), zone_text.size());
#endif


	if(msg_len < msg_header_size_B)
		throw glare::Exception("Invalid message size: " + toString(msg_len));

	// Handle ObjectInitialSendCompressed specially since we do a streaming read and decompression of its body.
	if(msg_type == Protocol::ObjectInitialSendCompressed)
	{
		// Do streaming decompression of objects

		ZoneScopedN("ClientThread: handling ObjectInitialSendCompressed"); // Tracy profiler

		if(msg_len < msg_header_size_B + sizeof(uint64)) // Message should contain decompressed_size uint64.
			throw glare::Exception("Invalid message size: " + toString(msg_len));

		const uint64 decompressed_size = socket->readUInt64();
		const size_t compressed_size = msg_len - (msg_header_size_B + sizeof(uint64)); // Compressed data is the rest of the message after the header and decompressed size

		if(decompressed_size > 300'000'000)
			throw glare::Exception("ObjectInitialSendCompressed: decompressed_size is too large: " + toString(decompressed_size)); 
		if(compressed_size > 100000000)
			throw glare::Exception("ObjectInitialSendCompressed: compressed_size is too large: " + toString(compressed_size)); 
		
		ZSTD_initDStream(dstream); // Call before new decompression operation using same DStream

		js::Vector<uint8, 16> compressed_buffer(compressed_size);
		js::Vector<uint8, 16> decompressed_buffer(decompressed_size);

		ZSTD_outBuffer out_buffer;
		out_buffer.dst = decompressed_buffer.data(); // start of output buffer
		out_buffer.size = decompressed_size; // size of output buffer
		out_buffer.pos = 0; // position where writing stopped. Will be updated. Necessarily 0 <= pos <= size

		ZSTD_inBuffer in_buffer;
		in_buffer.src = compressed_buffer.data(); // start of input buffer
		in_buffer.size = 0; // size of input buffer
		in_buffer.pos = 0; // position where reading stopped. Will be updated. Necessarily 0 <= pos <= size

		size_t next_ob_send_msg_start_i = 0; // Byte index in decompressed_buffer where the next ObjectInitialSend message will start.
		size_t num_obs_decoded = 0;

		const size_t max_read_chunk_size = 32768; // max number of bytes to read per socket read call.  The first zstd decoded data arrives after ~= 32768 input bytes, so no point reading much smaller amounts.
		for(size_t read_i=0; read_i<compressed_size; read_i += max_read_chunk_size)
		{
			// Read some data from network to compressed_buffer
			const size_t read_chunk_end = myMin(read_i + max_read_chunk_size, compressed_size);
			const size_t read_chunk_actual_size = read_chunk_end - read_i;

			socket->readData(/*dest buf=*/compressed_buffer.data() + read_i, /*num bytes=*/read_chunk_actual_size);
			in_buffer.size = read_chunk_end;

			// Do some decompression
			size_t res = ZSTD_decompressStream(dstream, &out_buffer, &in_buffer);
			if(ZSTD_isError(res))
				throw glare::Exception("Error from ZSTD_decompressStream(): " + std::string(ZSTD_getErrorName(res)));

			if(read_chunk_end == compressed_size) // If we have read all data from socket into compressed_buffer, make sure decompression is finished.
			{
				// We may need to call ZSTD_decompressStream one or more times to flush remaining data.  See https://facebook.github.io/zstd/zstd_manual.html#Chapter9
				int max_num_flush_iters = 1024;
				int iter = 0;
				while(out_buffer.pos < out_buffer.size)
				{
					res = ZSTD_decompressStream(dstream, &out_buffer, &in_buffer);
					if(ZSTD_isError(res))
						throw glare::Exception("Error from ZSTD_decompressStream(): " + std::string(ZSTD_getErrorName(res)));
					iter++;
					runtimeCheck(iter < max_num_flush_iters); // Avoid infinite loops on zstd failure or incorrect API usage.
				}
			}

			// conPrint("Read " + toString(read_chunk_end) + " / " + toString(compressed_size) + " B of compressed data, decoded " + toString(num_obs_decoded) + " obs");

			// Process any complete ObjectInitialSend messages in our decompressed data
			while(1)
			{
				if((out_buffer.pos - next_ob_send_msg_start_i) >= sizeof(uint32) * 2) // If we have read at least the header of the next ObjectInitialSend message:
				{
					uint32 sub_msg_header[2];
					std::memcpy(sub_msg_header, decompressed_buffer.data() + next_ob_send_msg_start_i, sizeof(uint32) * 2);
					if(sub_msg_header[0] != Protocol::ObjectInitialSend)
						throw glare::Exception("invalid sub-msg in ObjectInitialSendCompressed");
					
					const size_t sub_msg_len = sub_msg_header[1];
					if((sub_msg_len < msg_header_size_B) || (sub_msg_len > 1000000))
						throw glare::Exception("Invalid sub-message size: " + toString(sub_msg_len));

					if((next_ob_send_msg_start_i + sub_msg_len) <= out_buffer.pos) // If we have decompressed this entire sub-message:
					{
						// Process it
						BufferViewInStream sub_msg_buffer_view(ArrayRef<uint8>(decompressed_buffer.data() + next_ob_send_msg_start_i, sub_msg_len));
						sub_msg_buffer_view.advanceReadIndex(sizeof(uint32) * 2); // Skip header

						handleObjectInitialSend(sub_msg_buffer_view);

						num_obs_decoded++;
						next_ob_send_msg_start_i += sub_msg_len;
					}
					else
						break;
				}
				else
					break;
			}
		}

		// NOTE: do we need to flush the decompression stream here?

		// conPrint("ObjectInitialSendCompressed: Final num objects decoded: " + toString(num_obs_decoded));
		return;
	}

	if(msg_len > 1000000)
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
			const std::string new_lightmap_url = msg_buffer.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
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
			const std::string new_model_url = msg_buffer.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
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
	case Protocol::ObjectContentChanged:
		{
			//conPrint("ObjectContentChanged");
			const UID object_uid = readUIDFromStream(msg_buffer);
			const std::string new_content = msg_buffer.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);
			//conPrint("new_model_url: " + new_model_url);

			// Look up existing object in world state
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(object_uid);
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					ob->content = new_content;

					ob->from_remote_content_dirty = true;
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
			handleObjectInitialSend(msg_buffer);
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
	case Protocol::ParcelInitialSendCompressed:
		{
			ZoneScopedN("ClientThread: handling ParcelInitialSendCompressed"); // Tracy profiler

			// Decompress body with zstd
			BufferInStream temp_msg_buffer;
			decompressWithZstdTo(/*src=*/msg_buffer.currentReadPtr(), /*src len=*/msg_buffer.size() - msg_buffer.getReadIndex(), /*decompressed buf out=*/temp_msg_buffer.buf);

			{ // World state lock scope
				::Lock lock(world_state->mutex);

				while(!temp_msg_buffer.endOfStream())
				{
					// Read sub-msg type and length
					uint32 sub_msg_type_and_len[2];
					temp_msg_buffer.readData(sub_msg_type_and_len, sizeof(uint32) * 2);
					const uint32 sub_msg_type = sub_msg_type_and_len[0];
					const uint32 sub_msg_len  = sub_msg_type_and_len[1];
					if(sub_msg_type != Protocol::ParcelCreated)
						throw glare::Exception("sub-message was not ParcelCreated");

					if((sub_msg_len < sizeof(uint32)*2))
						throw glare::Exception("invalid sub message length");

					const size_t remaining_sub_msg_len = sub_msg_len - sizeof(uint32)*2;
					if(!temp_msg_buffer.canReadNBytes(remaining_sub_msg_len))
						throw glare::Exception("invalid sub message length");
					BufferViewInStream sub_buffer_view(ArrayRef<uint8>((const uint8*)temp_msg_buffer.currentReadPtr(), remaining_sub_msg_len));
					
					ParcelRef parcel = new Parcel();
					const ParcelID parcel_id = readParcelIDFromStream(sub_buffer_view);
					readFromNetworkStreamGivenID(sub_buffer_view, *parcel, peer_protocol_version);
					parcel->id = parcel_id;
					parcel->state = Parcel::State_JustCreated;
					parcel->from_remote_dirty = true;

					world_state->parcels[parcel->id] = parcel;
					world_state->dirty_from_remote_parcels.insert(parcel);

					temp_msg_buffer.advanceReadIndex(remaining_sub_msg_len);
				}
			} // End world state lock scope

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
			const URLString model_url = toURLString(msg_buffer.readStringLengthFirst(MAX_STRING_LEN));
			conPrint("Received GetFile message from server, model_url: '" + toStdString(model_url) + "'");

			out_msg_queue->enqueue(new GetFileMessage(model_url));
			break;
		}
	case Protocol::NewResourceOnServer:
		{
			//conPrint("Received NewResourceOnServer message from server.");
			const URLString url = toURLString(msg_buffer.readStringLengthFirst(MAX_STRING_LEN));
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
	case Protocol::WorldDetailsInitialSendMessage:
		{
			Reference<WorldDetailsReceivedMessage> msg = new WorldDetailsReceivedMessage();

			readWorldDetailsFromNetworkStream(msg_buffer, msg->world_details); // Read from msg_buffer, write to msg->world_details
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
	case Protocol::LODChunkInitialSend:
		{
			ZoneScopedN("ClientThread: handling LODChunkInitialSend"); // Tracy profiler

			LODChunkRef chunk = new LODChunk();
			readLODChunkFromStream(msg_buffer, *chunk);

			{
				Lock lock(world_state->mutex);
				auto res = world_state->lod_chunks.find(chunk->coords); // Look up existing chunk in world state
				if(res != world_state->lod_chunks.end()) // If chunk with given coords is present:
				{
					LODChunk* existing_chunk = res->second.getPointer();
					existing_chunk->copyNetworkStateFrom(*chunk);
					world_state->dirty_from_remote_lod_chunks.insert(existing_chunk);
				}
				else // Else if chunk with given coords is not present:
				{
					world_state->lod_chunks.insert(std::make_pair(chunk->coords, chunk)); // Add it to world state

					world_state->dirty_from_remote_lod_chunks.insert(chunk);
				}
			}
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
		TracyMessageL("ClientThread: TLSSocket connected");


		// Do initial query-response part of protocol

		SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		scratch_packet.writeUInt32(Protocol::CyberspaceHello); // Write hello
		scratch_packet.writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		scratch_packet.writeUInt32(Protocol::ConnectionTypeUpdates); // Write connection type
		scratch_packet.writeStringLengthFirst(world_name); // Write world name
		socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());


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

		// Read server capabilities
		uint32 server_capabilities = 0;
		if(peer_protocol_version >= 41)
			server_capabilities = socket->readUInt32();

		// Read server_mesh_optimisation_version
		int server_mesh_optimisation_version = 1;
		if(peer_protocol_version >= 43)
			server_mesh_optimisation_version = socket->readInt32();

		// Read assigned client avatar UID
		this->client_avatar_uid = readUIDFromStream(*socket);

		if(peer_protocol_version >= 42)
		{
			// Send client capabilities
			const uint32 client_capabilities = Protocol::STREAMING_COMPRESSED_OBJECT_SUPPORT;
			socket->writeUInt32(client_capabilities);
		}

		TracyMessageL("ClientThread: read initial data");

		out_msg_queue->enqueue(new ClientConnectedToServerMessage(this->client_avatar_uid, peer_protocol_version, server_capabilities, server_mesh_optimisation_version));

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
