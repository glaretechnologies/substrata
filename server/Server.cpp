/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "WorkerThread.h"
#include "../shared/Protocol.h"
#include "../networking/networking.h"
#include <ThreadManager.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Parser.h>
#include <Base64.h>
#include <ArgumentParser.h>
#include <SocketBufferOutStream.h>
#include <TLSSocket.h>
#include <MTwister.h>


static const int parcel_coords[10][4][2] ={
	{ { 5, 50 },{ 25, 50 },{ 25, 70 },{ 5, 70 } }, // 0
	{ { 25, 50 },{ 45, 50 },{ 45, 70 },{ 25, 70 } }, // 1
	{ { 45, 50 },{ 45, 50 },{ 65, 70 },{ 45, 70 } }, // 2
	{ { 5, 70 },{ 25, 70 },{ 25, 90 },{ 5, 90 } }, // 3
	{ { 25, 70 },{ 45, 70 },{ 45, 90 },{ 25, 90 } }, // 4
	{ { 45, 70 },{ 65, 70 },{ 65, 90 },{ 45, 90 } }, // 5
	{ { 45, 90 },{ 65, 90 },{ 65, 115 },{ 45, 115 } }, // 6
	{ { 5, 115 },{ 25, 115 },{ 25, 135 },{ 5, 135 } }, // 7
	{ { 25, 115 },{ 45, 115 },{ 45, 135 },{ 25, 135 } }, // 8
	{ { 45, 115 },{ 65, 115 },{ 65, 135 },{ 45, 135 } }, // 9
};

static void makeParcels(Matrix2d M, int& next_id, Reference<ServerWorldState> world_state)
{
	// Add up then right parcels
	for(int i=0; i<10; ++i)
	{
		const ParcelID parcel_id(next_id++);
		ParcelRef test_parcel = new Parcel();
		test_parcel->state = Parcel::State_Alive;
		test_parcel->id = parcel_id;
		test_parcel->owner_id = UserID(0);
		test_parcel->admin_ids.push_back(UserID(0));
		test_parcel->writer_ids.push_back(UserID(0));
		test_parcel->created_time = TimeStamp::currentTime();
		test_parcel->zbounds = Vec2d(-1, 1);

		for(int v=0; v<4; ++v)
			test_parcel->verts[v] = M * Vec2d(parcel_coords[i][v][0], parcel_coords[i][v][1]);

		world_state->parcels[parcel_id] = test_parcel;
	}
}


static void makeBlock(const Vec2d& botleft, MTwister& rng, int& next_id, Reference<ServerWorldState> world_state)
{
	// Randomly omit one of the 4 edge blocks
	const int e = (int)(rng.unitRandom() * 3.9999);
	for(int xi=0; xi<3; ++xi)
		for(int yi=0; yi<3; ++yi)
		{
			if(xi == 1 && yi == 1)
			{
				// Leave middle of block empty.
			}
			else if(xi == 1 && yi == 0 && e == 0)
			{
			}
			else if(xi == 2 && yi == 1 && e == 1)
			{
			}
			else if(xi == 1 && yi == 2 && e == 2)
			{
			}
			else if(xi == 0 && yi == 1 && e == 3)
			{
			}
			else
			{
				const ParcelID parcel_id(next_id++);
				ParcelRef test_parcel = new Parcel();
				test_parcel->state = Parcel::State_Alive;
				test_parcel->id = parcel_id;
				test_parcel->owner_id = UserID(0);
				test_parcel->admin_ids.push_back(UserID(0));
				test_parcel->writer_ids.push_back(UserID(0));
				test_parcel->created_time = TimeStamp::currentTime();
				test_parcel->zbounds = Vec2d(-1, 1);

				test_parcel->verts[0] = botleft + Vec2d(xi * 20, yi * 20);
				test_parcel->verts[1] = botleft + Vec2d((xi+1)* 20, yi * 20);
				test_parcel->verts[2] = botleft + Vec2d((xi+1)* 20, (yi+1) * 20);
				test_parcel->verts[3] = botleft + Vec2d((xi)* 20, (yi+1) * 20);

				world_state->parcels[parcel_id] = test_parcel;
			}
		}
}


static void enqueuePacketToBroadcast(SocketBufferOutStream& packet_buffer, std::vector<std::string>& broadcast_packets)
{
	if(packet_buffer.buf.size() > 0)
	{
		std::string packet_string(packet_buffer.buf.size(), '\0');

		std::memcpy(&packet_string[0], packet_buffer.buf.data(), packet_buffer.buf.size());

		broadcast_packets.push_back(packet_string);
	}
}


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	TLSSocket::initTLS();

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--src_resource_dir"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax);

		// src_resource_dir can be set to something like C:\programming\chat_site\trunk to read e.g. script.js directly from trunk
		std::string src_resource_dir = "./";
		if(parsed_args.isArgPresent("--src_resource_dir"))
			src_resource_dir = parsed_args.getArgStringValue("--src_resource_dir");

		conPrint("src_resource_dir: '" + src_resource_dir + "'");

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
#if BUILD_TESTS
			Base64::test();
			Parser::doUnitTests();
			conPrint("----Finished tests----");
#endif
			return 0;
		}
		//-----------------------------------------------------------------------------------------

		const int listen_port = 7600;
		conPrint("listen port: " + toString(listen_port));

#if _WIN32
		const std::string server_state_dir = "D:/cyberspace_server_state";
#else
		const std::string server_state_dir = "/home/nick/cyberspace_server_state";
#endif

		const std::string server_resource_dir = server_state_dir + "/server_resources";
		conPrint("server_resource_dir: " + server_resource_dir);


		FileUtils::createDirIfDoesNotExist(server_resource_dir);
		
		Server server;
		server.resource_manager = new ResourceManager(server_resource_dir);

		const std::string server_state_path = server_state_dir + "/server_state.bin";
		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);

		// Add a teapot object
		WorldObjectRef test_object;
		if(true)
		{
			const UID uid(6000);
			test_object = new WorldObject();
			test_object->state = WorldObject::State_Alive;
			test_object->uid = uid;
			test_object->pos = Vec3d(3, 0, 1);
			test_object->angle = 0;
			test_object->axis = Vec3f(1,0,0);
			test_object->model_url = "teapot_obj_12507117953098989663.obj";
			test_object->scale = Vec3f(1.f);
			server.world_state->objects[uid] = test_object;
		}

		// TEMP: Add a parcel
		if(false)
		{
			const ParcelID parcel_id(7000);
			ParcelRef test_parcel = new Parcel();
			test_parcel->state = Parcel::State_Alive;
			test_parcel->id = parcel_id;
			test_parcel->owner_id = UserID(0);
			test_parcel->admin_ids.push_back(UserID(0));
			test_parcel->writer_ids.push_back(UserID(0));
			test_parcel->created_time = TimeStamp::currentTime();
			test_parcel->aabb_min = Vec3d(30, 30, -10);
			test_parcel->aabb_max = Vec3d(60, 60, 60);
			test_parcel->description = "This is a pretty cool parcel.";
			server.world_state->parcels[parcel_id] = test_parcel;
		}

		
		// Add 'town square' parcels
		if(true)
		{
			server.world_state->parcels.clear();

			int next_id = 10;
			makeParcels(Matrix2d(1, 0, 0, 1), next_id, server.world_state);
			makeParcels(Matrix2d(-1, 0, 0, 1), next_id, server.world_state); // Mirror in y axis (x' = -x)
			makeParcels(Matrix2d(0, 1, 1, 0), next_id, server.world_state); // Mirror in x=y line(x' = y, y' = x)
			makeParcels(Matrix2d(0, 1, -1, 0), next_id, server.world_state); // Rotate right 90 degrees (x' = y, y' = -x)
			makeParcels(Matrix2d(1, 0, 0, -1), next_id, server.world_state); // Mirror in x axis (y' = -y)
			makeParcels(Matrix2d(-1, 0, 0, -1), next_id, server.world_state); // Rotate 180 degrees (x' = -x, y' = -y)
			makeParcels(Matrix2d(0, -1, -1, 0), next_id, server.world_state); // Mirror in x=-y line (x' = -y, y' = -x)
			makeParcels(Matrix2d(0, -1, 1, 0), next_id, server.world_state); // Rotate left 90 degrees (x' = -y, y' = x)

			MTwister rng(1);
			const int D = 4;
			for(int x=-D; x<D; ++x)
				for(int y=-D; y<D; ++y)
				{
					if(x >= -2 && x <= 1 && y >= -2 && y <= 1)// && 
						//!(x == -2 && -y == 2) && !(x == 1 && y == 1) && !(x == -2 && y == 1) && !(x == 1 && y == -2))
					{
						// Special town square blocks
					}
					else
						makeBlock(Vec2d(5 + x*70, 5 + y*70), rng, next_id, server.world_state);
				}
			
			
		}

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		Timer save_state_timer;

		// Main server loop
		while(1)
		{
			PlatformUtils::Sleep(100);

			std::vector<std::string> broadcast_packets;

			Lock lock(server.world_state->mutex);

			//TEMP:
			/*{
				const double theta = Clock::getCurTimeRealSec();
				test_object->pos = Vec3d(sin(theta), 2, cos(theta) + 1);
				test_object->from_remote_transform_dirty = true;
			}*/

			// Generate packets for avatar changes
			for(auto i = server.world_state->avatars.begin(); i != server.world_state->avatars.end();)
			{
				Avatar* avatar = i->second.getPointer();
				if(avatar->other_dirty)
				{
					if(avatar->state == Avatar::State_Alive)
					{
						// Send AvatarFullUpdate packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::AvatarFullUpdate);
						writeToNetworkStream(*avatar, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->other_dirty = false;
						avatar->transform_dirty = false;
						i++;
					}
					else if(avatar->state == Avatar::State_JustCreated)
					{
						// Send AvatarCreated packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::AvatarCreated);
						writeToStream(avatar->uid, packet);
						packet.writeStringLengthFirst(avatar->name);
						packet.writeStringLengthFirst(avatar->model_url);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->rotation, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->state = Avatar::State_Alive;
						avatar->other_dirty = false;
						avatar->transform_dirty = false;

						i++;
					}
					else if(avatar->state == Avatar::State_Dead)
					{
						// Send AvatarDestroyed packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::AvatarDestroyed);
						writeToStream(avatar->uid, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						// Remove avatar from avatar map
						auto old_avatar_iterator = i;
						i++;
						server.world_state->avatars.erase(old_avatar_iterator);

						conPrint("Removed avatar from world_state->avatars");
					}
					else
					{
						assert(0);
					}
				}
				else if(avatar->transform_dirty)
				{
					if(avatar->state == Avatar::State_Alive)
					{
						// Send AvatarTransformUpdate packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::AvatarTransformUpdate);
						writeToStream(avatar->uid, packet);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->rotation, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->transform_dirty = false;
					}
					i++;
				}
				else
				{
					i++;
				}
			}



			// Generate packets for object changes
			for(auto i = server.world_state->objects.begin(); i != server.world_state->objects.end();)
			{
				WorldObject* ob = i->second.getPointer();
				if(ob->from_remote_other_dirty)
				{
					conPrint("Object 'other' dirty, sending full update");

					if(ob->state == WorldObject::State_Alive)
					{
						// Send ObjectFullUpdate packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::ObjectFullUpdate);
						writeToNetworkStream(*ob, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->from_remote_other_dirty = false;
						ob->from_remote_transform_dirty = false; // transform is sent in full packet also.
						server.world_state->changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_JustCreated)
					{
						// Send ObjectCreated packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::ObjectCreated);
						writeToNetworkStream(*ob, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->state = WorldObject::State_Alive;
						ob->from_remote_other_dirty = false;
						server.world_state->changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_Dead)
					{
						// Send ObjectDestroyed packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::ObjectDestroyed);
						writeToStream(ob->uid, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						// Remove ob from object map
						auto old_ob_iterator = i;
						i++;
						server.world_state->objects.erase(old_ob_iterator);

						conPrint("Removed object from world_state->objects");
						server.world_state->changed = true;
					}
					else
					{
						conPrint("ERROR: invalid object state.");
						assert(0);
					}
				}
				else if(ob->from_remote_transform_dirty)
				{
					//conPrint("Object 'transform' dirty, sending transform update");

					if(ob->state == WorldObject::State_Alive)
					{
						// Send ObjectTransformUpdate packet
						SocketBufferOutStream packet(/*use_network_byte_order=*/false);
						packet.writeUInt32(Protocol::ObjectTransformUpdate);
						writeToStream(ob->uid, packet);
						writeToStream(ob->pos, packet);
						writeToStream(ob->axis, packet);
						packet.writeFloat(ob->angle);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->from_remote_transform_dirty = false;
						server.world_state->changed = true;
					}
					i++;
				}
				else
				{
					i++;
				}
			}

			// Enqueue packets to worker threads to send
			{
				Lock lock2(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					for(size_t z=0; z<broadcast_packets.size(); ++z)
					{
						assert(dynamic_cast<WorkerThread*>(i->getPointer()));
						static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(broadcast_packets[z]);
					}
				}
			}


			if(server.world_state->changed && (save_state_timer.elapsed() > 5.0))
			{
				try
				{
					// Save world state to disk
					Lock lock2(server.world_state->mutex);

					server.world_state->serialiseToDisk(server_state_path);

					server.world_state->changed = false;
					save_state_timer.reset();
				}
				catch(Indigo::Exception& e)
				{
					conPrint("Warning: saving world state to disk failed: " + e.what());
					save_state_timer.reset(); // Reset timer so we don't try again straight away.
				}
			}

		} // End of main server loop
	}
	catch(ArgumentParserExcep& e)
	{
		conPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
		return 1;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("FileUtils::FileUtilsExcep: " + e.what());
		return 1;
	}

	Networking::destroyInstance();
	return 0;
}


Server::Server()
{
	world_state = new ServerWorldState();
}
