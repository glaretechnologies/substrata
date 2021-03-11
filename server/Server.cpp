/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "WorkerThread.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../networking/Networking.h"
#include <ThreadManager.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Parser.h>
#include <Base64.h>
#include <CryptoRNG.h>
#include <ArgumentParser.h>
#include <SocketBufferOutStream.h>
#include <TLSSocket.h>
#include <PCG32.h>
#include <Matrix4f.h>
#include <Quat.h>
#include <OpenSSL.h>
#include <tls.h>
#include <networking/HTTPClient.h>//TEMP for testing
#include "../webserver/WebServerRequestHandler.h"
#include "../webserver/WebDataStore.h"
#include <WebListenerThread.h>


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
		test_parcel->zbounds = Vec2d(-1, 10);

		for(int v=0; v<4; ++v)
			test_parcel->verts[v] = M * Vec2d(parcel_coords[i][v][0], parcel_coords[i][v][1]);

		world_state->parcels[parcel_id] = test_parcel;
	}
}


static void makeBlock(const Vec2d& botleft, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state)
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
				test_parcel->zbounds = Vec2d(-1, 10);

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


static void assignParcelToUser(const Reference<ServerWorldState>& world_state, const ParcelID& parcel_id, const UserID& user_id)
{
	conPrint("Assigning parcel " + parcel_id.toString() + " to user " + user_id.toString());

	if(world_state->parcels.count(parcel_id) != 0)
	{
		ParcelRef parcel = world_state->parcels.find(parcel_id)->second;

		parcel->owner_id = user_id;
		parcel->admin_ids = std::vector<UserID>(1, user_id);
		parcel->writer_ids = std::vector<UserID>(1, user_id);

		conPrint("\tDone.");
	}
	else
	{
		conPrint("\tFailed, parcel not found.");
	}
}


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();

	conPrint("Substrata server v" + ::cyberspace_version);

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
			CryptoRNG::test();
			StringUtils::test();
			//HTTPClient::test();
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
		const std::string substrata_appdata_dir = PlatformUtils::getOrCreateAppDataDirectory("Substrata");
		const std::string server_state_dir = substrata_appdata_dir + "/server_data";
#else
		const std::string username = PlatformUtils::getLoggedInUserName();
		const std::string server_state_dir = "/home/" + username + "/cyberspace_server_state";
#endif

		FileUtils::createDirIfDoesNotExist(server_state_dir);

		const std::string server_resource_dir = server_state_dir + "/server_resources";
		conPrint("server_resource_dir: " + server_resource_dir);


		FileUtils::createDirIfDoesNotExist(server_resource_dir);
		
		Server server;
		server.world_state->resource_manager = new ResourceManager(server_resource_dir);

#ifdef WIN32
		server.screenshot_dir = "C:\\programming\\new_cyberspace\\webdata\\screenshots"; // Dir generated screenshots will be saved to.
#else
		server.screenshot_dir = "/var/www/cyberspace/screenshots";
#endif
		FileUtils::createDirIfDoesNotExist(server.screenshot_dir);


		const std::string server_state_path = server_state_dir + "/server_state.bin";
		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);


		//server.world_state->updateFromDatabase();

		//TEMP:
		//server.world_state->resource_manager->getResourcesForURL().clear();

		// Add a teapot object
		WorldObjectRef test_object;
		if(false)
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
			//server.world_state->objects[uid] = test_object;
		}

		//TEMP
		//server.world_state->parcels.clear();

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
			test_parcel->verts[0] = Vec2d(-10, -10);
			test_parcel->verts[1] = Vec2d( 10, -10);
			test_parcel->verts[2] = Vec2d( 10,  10);
			test_parcel->verts[3] = Vec2d(-10,  10);
			test_parcel->zbounds = Vec2d(-1, 10);
			test_parcel->build();

			test_parcel->description = "This is a pretty cool parcel.";
			//server.world_state->parcels[parcel_id] = test_parcel;
		}

		
		// Add 'town square' parcels
		if(server.world_state->getRootWorldState()->parcels.empty())
		{
			conPrint("Adding some parcels!");

			int next_id = 10;
			makeParcels(Matrix2d(1, 0, 0, 1), next_id, server.world_state->getRootWorldState());
			makeParcels(Matrix2d(-1, 0, 0, 1), next_id, server.world_state->getRootWorldState()); // Mirror in y axis (x' = -x)
			makeParcels(Matrix2d(0, 1, 1, 0), next_id, server.world_state->getRootWorldState()); // Mirror in x=y line(x' = y, y' = x)
			makeParcels(Matrix2d(0, 1, -1, 0), next_id, server.world_state->getRootWorldState()); // Rotate right 90 degrees (x' = y, y' = -x)
			makeParcels(Matrix2d(1, 0, 0, -1), next_id, server.world_state->getRootWorldState()); // Mirror in x axis (y' = -y)
			makeParcels(Matrix2d(-1, 0, 0, -1), next_id, server.world_state->getRootWorldState()); // Rotate 180 degrees (x' = -x, y' = -y)
			makeParcels(Matrix2d(0, -1, -1, 0), next_id, server.world_state->getRootWorldState()); // Mirror in x=-y line (x' = -y, y' = -x)
			makeParcels(Matrix2d(0, -1, 1, 0), next_id, server.world_state->getRootWorldState()); // Rotate left 90 degrees (x' = -y, y' = x)

			PCG32 rng(1);
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
						makeBlock(Vec2d(5 + x*70, 5 + y*70), rng, next_id, server.world_state->getRootWorldState());
				}
		}

		// TEMP: make all parcels have zmax = 10
		{
			for(auto i = server.world_state->getRootWorldState()->parcels.begin(); i != server.world_state->getRootWorldState()->parcels.end(); ++i)
			{
				ParcelRef parcel = i->second;
				parcel->zbounds.y = 10.0f;
			}
		}

		// TEMP: Print out users
		for(auto i = server.world_state->user_id_to_users.begin(); i != server.world_state->user_id_to_users.end(); ++i)
			conPrint("User with id " + i->second->id.toString() + ": " + i->second->name);

		// TEMP: Assign some parcel permissions
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(10), UserID(1));
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(11), UserID(2)); // dirtypunk
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(12), UserID(3)); // zom-b
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(15), UserID(3)); // zom-b
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(32), UserID(4)); // lycium
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(35), UserID(4)); // lycium		
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(31), UserID(5)); // Harry
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(41), UserID(6)); // Originalplan
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(40), UserID(8)); // trislit
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(30), UserID(9)); // fused
		assignParcelToUser(server.world_state->getRootWorldState(), ParcelID(50), UserID(23)); // cody2343

		// Make parcel with id 20 a 'sandbox', world-writeable parcel
		{
			auto res = server.world_state->getRootWorldState()->parcels.find(ParcelID(20));
			if(res != server.world_state->getRootWorldState()->parcels.end())
			{
				res->second->all_writeable = true;
				conPrint("Made parcel 20 all-writeable.");
			}
		}


		//server.world_state->objects.clear();



		server.world_state->denormaliseData();



		//-------------------------------- Launch webserver ---------------------------------------------------------

		// Create TLS configuration
		struct tls_config* web_tls_configuration = tls_config_new();

#ifdef WIN32
		// Skip TLS stuff when testing on windows for now.
#else
		if(tls_config_set_cert_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");
		 // set private key
		if(tls_config_set_key_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#endif

		Reference<WebDataStore> web_data_store = new WebDataStore();

#ifdef WIN32		
		web_data_store->public_files_dir = "C:\\programming\\new_cyberspace\\webdata\\public_files";
		//web_data_store->resources_dir    = "C:\\programming\\new_cyberspace\\webdata\\resources";
		web_data_store->letsencrypt_webroot = "C:\\programming\\new_cyberspace\\webdata\\letsencrypt_webroot";
#else
		web_data_store->public_files_dir = "/var/www/cyberspace/public_html";
		//web_data_store->resources_dir    = "/var/www/cyberspace/resources";
		web_data_store->letsencrypt_webroot = "/var/www/cyberspace/letsencrypt_webroot";
#endif

		Reference<WebServerSharedRequestHandler> shared_request_handler = new WebServerSharedRequestHandler();
		shared_request_handler->data_store = web_data_store.ptr();
		shared_request_handler->world_state = server.world_state.ptr();

		/*if(FileUtils::fileExists(data_store->path))
		data_store->loadFromDisk();
		else
		conPrint(data_store->path + " not found!");*/

		ThreadManager web_thread_manager;
		web_thread_manager.addThread(new web::WebListenerThread(80,  shared_request_handler.getPointer(), NULL));
		web_thread_manager.addThread(new web::WebListenerThread(443, shared_request_handler.getPointer(), web_tls_configuration));


		//-----------------------------------------------------------------------------------------


		// Create TLS configuration for substrata protocol server
		struct tls_config* tls_configuration = tls_config_new();

#ifdef WIN32
		// NOTE: key generated with 
		// cd D:\programming\LibreSSL\libressl-2.8.3-x64-vs2019-install\bin
		// openssl req -new -newkey rsa:4096 -x509 -sha256 -days 3650 -nodes -out MyCertificate.crt -keyout MyKey.key
		if(tls_config_set_cert_file(tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");
		
		if(tls_config_set_key_file(tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#else
		// For now just use our web Let's Encrypt cert and private key.
		if(tls_config_set_cert_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");

		if(tls_config_set_key_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#endif

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server, tls_configuration));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		Timer save_state_timer;

		// A map from world name to a vector of packets to send to clients connected to that world.
		std::map<std::string, std::vector<std::string>> broadcast_packets;

		// Main server loop
		uint64 loop_iter = 0;
		while(1)
		{
			PlatformUtils::Sleep(100);

			//broadcast_packets.clear();

			{ // Begin scope for world_state->mutex lock

				Lock lock(server.world_state->mutex);

				for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
				{
					Reference<ServerWorldState> world_state = world_it->second;

					std::vector<std::string>& world_packets = broadcast_packets[world_it->first];

					// Generate packets for avatar changes
					for(auto i = world_state->avatars.begin(); i != world_state->avatars.end();)
					{
						Avatar* avatar = i->second.getPointer();
						if(avatar->other_dirty)
						{
							if(avatar->state == Avatar::State_Alive)
							{
								// Send AvatarFullUpdate packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::AvatarFullUpdate);
								writeToNetworkStream(*avatar, packet);

								enqueuePacketToBroadcast(packet, world_packets);

								avatar->other_dirty = false;
								avatar->transform_dirty = false;
								i++;
							}
							else if(avatar->state == Avatar::State_JustCreated)
							{
								// Send AvatarCreated packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::AvatarCreated);
								writeToStream(avatar->uid, packet);
								packet.writeStringLengthFirst(avatar->name);
								packet.writeStringLengthFirst(avatar->model_url);
								writeToStream(avatar->pos, packet);
								writeToStream(avatar->rotation, packet);

								enqueuePacketToBroadcast(packet, world_packets);

								avatar->state = Avatar::State_Alive;
								avatar->other_dirty = false;
								avatar->transform_dirty = false;

								i++;
							}
							else if(avatar->state == Avatar::State_Dead)
							{
								// Send AvatarDestroyed packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::AvatarDestroyed);
								writeToStream(avatar->uid, packet);

								enqueuePacketToBroadcast(packet, world_packets);

								// Remove avatar from avatar map
								auto old_avatar_iterator = i;
								i++;
								world_state->avatars.erase(old_avatar_iterator);

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
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::AvatarTransformUpdate);
								writeToStream(avatar->uid, packet);
								writeToStream(avatar->pos, packet);
								writeToStream(avatar->rotation, packet);

								enqueuePacketToBroadcast(packet, world_packets);

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
					for(auto i = world_state->dirty_from_remote_objects.begin(); i != world_state->dirty_from_remote_objects.end(); ++i)
					{
						WorldObject* ob = i->ptr();
						if(ob->from_remote_other_dirty)
						{
							// conPrint("Object 'other' dirty, sending full update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectFullUpdate packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::ObjectFullUpdate);
								ob->writeToNetworkStream(packet);

								enqueuePacketToBroadcast(packet, world_packets);

								ob->from_remote_other_dirty = false;
								ob->from_remote_transform_dirty = false; // transform is sent in full packet also.
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_JustCreated)
							{
								// Send ObjectCreated packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::ObjectCreated);
								ob->writeToNetworkStream(packet);

								enqueuePacketToBroadcast(packet, world_packets);

								ob->state = WorldObject::State_Alive;
								ob->from_remote_other_dirty = false;
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_Dead)
							{
								// Send ObjectDestroyed packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::ObjectDestroyed);
								writeToStream(ob->uid, packet);

								enqueuePacketToBroadcast(packet, world_packets);

								// Remove ob from object map
								world_state->objects.erase(ob->uid);

								conPrint("Removed object from world_state->objects");
								server.world_state->markAsChanged();
							}
							else
							{
								conPrint("ERROR: invalid object state (ob->state=" + toString(ob->state) + ")");
								assert(0);
							}
						}
						else if(ob->from_remote_transform_dirty)
						{
							//conPrint("Object 'transform' dirty, sending transform update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectTransformUpdate packet
								SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
								packet.writeUInt32(Protocol::ObjectTransformUpdate);
								writeToStream(ob->uid, packet);
								writeToStream(ob->pos, packet);
								writeToStream(ob->axis, packet);
								packet.writeFloat(ob->angle);

								enqueuePacketToBroadcast(packet, world_packets);

								ob->from_remote_transform_dirty = false;
								server.world_state->markAsChanged();
							}
						}
						else if(ob->from_remote_lightmap_url_dirty)
						{
							// Send ObjectResourceURLChanged packet
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ObjectLightmapURLChanged);
							writeToStream(ob->uid, packet);
							packet.writeStringLengthFirst(ob->lightmap_url);

							enqueuePacketToBroadcast(packet, world_packets);

							ob->from_remote_lightmap_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_flags_dirty)
						{
							// Send ObjectFlagsChanged packet
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ObjectFlagsChanged);
							writeToStream(ob->uid, packet);
							packet.writeUInt32(ob->flags);

							enqueuePacketToBroadcast(packet, world_packets);

							ob->from_remote_flags_dirty = false;
							server.world_state->markAsChanged();
						}

					}

					world_state->dirty_from_remote_objects.clear();
				} // End for each server world

			} // End scope for world_state->mutex lock

			// Enqueue packets to worker threads to send
			// For each connected client, get packets for the world the client is connected to, and send to them.
			{
				Lock lock2(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					WorkerThread* worker = static_cast<WorkerThread*>(i->getPointer());
					std::vector<std::string>& packets = broadcast_packets[worker->connected_world_name];

					for(size_t z=0; z<packets.size(); ++z)
						worker->enqueueDataToSend(packets[z]);
				}
			}

			// Clear broadcast_packets
			for(auto it = broadcast_packets.begin(); it != broadcast_packets.end(); ++it)
				it->second.clear();
			
			if((loop_iter % 40) == 0) // Approx every 4 s.
			{
				// Send out TimeSyncMessage packets to clients
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::TimeSyncMessage);
				packet.writeDouble(server.getCurrentGlobalTime());
				std::string packet_string(packet.buf.size(), '\0');
				std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

				Lock lock3(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					assert(dynamic_cast<WorkerThread*>(i->getPointer()));
					static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet_string);
				}
			}


			if(server.world_state->hasChanged() && (save_state_timer.elapsed() > 60.0))
			{
				try
				{
					// Save world state to disk
					Lock lock2(server.world_state->mutex);

					server.world_state->serialiseToDisk(server_state_path);

					server.world_state->clearChangedFlag();
					save_state_timer.reset();
				}
				catch(glare::Exception& e)
				{
					conPrint("Warning: saving world state to disk failed: " + e.what());
					save_state_timer.reset(); // Reset timer so we don't try again straight away.
				}
			}


			loop_iter++;
		} // End of main server loop
	}
	catch(ArgumentParserExcep& e)
	{
		conPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(glare::Exception& e)
	{
		conPrint("glare::Exception: " + e.what());
		return 1;
	}

	Networking::destroyInstance();
	return 0;
}


Server::Server()
{
	world_state = new ServerAllWorldsState();
}


double Server::getCurrentGlobalTime() const
{
	return Clock::getTimeSinceInit();
}
