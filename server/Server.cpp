/*=====================================================================
Server.cpp
----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "UDPHandlerThread.h"
#include "MeshLODGenThread.h"
#include "DynamicTextureUpdaterThread.h"
//#include "ChunkGenThread.h"
#include "WorkerThread.h"
#include "ServerTestSuite.h"
#include "WorldCreation.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../shared/MessageUtils.h"
#include "../webserver/WebServerRequestHandler.h"
#include "../webserver/AccountHandlers.h"
#include "../webserver/WebDataStore.h"
#include "../webserver/WebDataFileWatcherThread.h"
#if USE_GLARE_PARCEL_AUCTION_CODE
#include <webserver/CoinbasePollerThread.h>
#include <webserver/OpenSeaPollerThread.h>
#include <server/AuctionManagement.h>
#endif
#include <webserver/WebListenerThread.h>
#include <networking/Networking.h>
#include <networking/TLSSocket.h>
#include <maths/PCG32.h>
#include <maths/Matrix4f.h>
#include <maths/Quat.h>
#include <maths/Rect2.h>
#include <utils/ThreadManager.h>
#include <utils/PlatformUtils.h>
#include <utils/Clock.h>
#include <utils/Timer.h>
#include <utils/FileUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/Parser.h>
#include <utils/XMLParseUtils.h>
#include <utils/IndigoXMLDoc.h>
#include <utils/ArgumentParser.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/OpenSSL.h>
#include <tls.h>


void updateMapTiles(ServerAllWorldsState& world_state)
{
	uint64 next_shot_id = world_state.getNextScreenshotUID();

	const int z_begin = 0;
	const int z_end = 7;
	if(true) // world_state.map_tile_info.empty())
	{
		// world_state.map_tile_info.clear();

		for(int z = z_begin; z < z_end; ++z)
		{
			const float TILE_WIDTH_M = 5120.f / (1 << z); //TILE_WIDTH_PX * metres_per_pixel;
			//const float TILE_WIDTH_M = 2560.f/*5120.f*/ / (1 << z); //TILE_WIDTH_PX * metres_per_pixel;

			const int span = (int)std::ceil(300 / TILE_WIDTH_M);
			const int plus_x_span = (int)std::ceil(700 / TILE_WIDTH_M);  // NOTE: pushing out positive x span here to encompass east districts
			const int plus_y_span = (int)std::ceil(530 / TILE_WIDTH_M);  // NOTE: pushing out positive y span here to encompass north district


			// We want zoom level 3 to have (half) span 2 = 2^1.
			// zoom level 4 : span = 2^(4-2) = 2^2 = 4.
			// So num tiles = (4*2)^2 = 64
			// zoom level 5 : span = 2^(5-2) = 2^3 = 8.
			// So num tiles = (8*2)^2 = 256

			// in general num_tiles = (span*2)^2 = ((2^(z-2))*2)^2 = (2^(z-1))^2 = 2^((z-1)*2) = 2^(2z - 2)
			// zoom level 6: num_tiles = 2^10 = 1024
			//const int span = 1 << myMax(0, z - 2); // 2^(z-2)

			const int x_begin = -span;
			const int x_end = plus_x_span;
			const int y_begin = -span;
			const int y_end = plus_y_span;

			

			for(int y = y_begin; y < y_end; ++y)
			for(int x = x_begin; x < x_end; ++x)
			{
				const Vec3<int> v(x, y, z);

				if(world_state.map_tile_info.info.count(v) == 0)
				{

					TileInfo info;
					info.cur_tile_screenshot = new Screenshot();
					info.cur_tile_screenshot->id = next_shot_id++;
					info.cur_tile_screenshot->created_time = TimeStamp::currentTime();
					info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
					info.cur_tile_screenshot->is_map_tile = true;
					info.cur_tile_screenshot->tile_x = x;
					info.cur_tile_screenshot->tile_y = y;
					info.cur_tile_screenshot->tile_z = z;

					world_state.map_tile_info.info[v] = info;

					conPrint("Added map tile screenshot: " + v.toString());

					world_state.markAsChanged();

					world_state.map_tile_info.db_dirty = true;
				}
			}
		}
	}
	else
	{
		// TEMP: Redo screenshot
		/*for(auto it = world_state.map_tile_info.begin(); it != world_state.map_tile_info.end(); ++it)
		{
			it->second.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
		}*/
	}
}


static void enqueueMessageToBroadcast(SocketBufferOutStream& packet_buffer, std::vector<std::string>& broadcast_packets)
{
	MessageUtils::updatePacketLengthField(packet_buffer);

	if(packet_buffer.buf.size() > 0)
	{
		std::string packet_string(packet_buffer.buf.size(), '\0');

		std::memcpy(&packet_string[0], packet_buffer.buf.data(), packet_buffer.buf.size());

		broadcast_packets.push_back(packet_string);
	}
}


// Throws glare::Exception on failure.
static ServerCredentials parseServerCredentials(const std::string& server_state_dir)
{
	const std::string path = server_state_dir + "/substrata_server_credentials.txt";

	const std::string contents = FileUtils::readEntireFileTextMode(path);

	ServerCredentials creds;

	Parser parser(contents);

	while(!parser.eof())
	{
		string_view key, value;
		if(!parser.parseToChar(':', key))
			throw glare::Exception("Error parsing key from '" + path + "'.");
		if(!parser.parseChar(':'))
			throw glare::Exception("Error parsing ':' from '" + path + "'.");

		parser.parseWhiteSpace();
		parser.parseLine(value);

		creds.creds[toString(key)] = ::stripHeadAndTailWhitespace(toString(value));
	}

	return creds;
}


static ServerConfig parseServerConfig(const std::string& config_path)
{
	IndigoXMLDoc doc(config_path);
	pugi::xml_node root_elem = doc.getRootElement();

	ServerConfig config;
	config.webserver_fragments_dir		= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_fragments_dir", /*default val=*/"");
	config.webserver_public_files_dir	= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_public_files_dir", /*default val=*/"");
	config.webclient_dir				= XMLParseUtils::parseStringWithDefault(root_elem, "webclient_dir", /*default val=*/"");
	config.tls_certificate_path			= XMLParseUtils::parseStringWithDefault(root_elem, "tls_certificate_path", /*default val=*/"");
	config.tls_private_key_path			= XMLParseUtils::parseStringWithDefault(root_elem, "tls_private_key_path", /*default val=*/"");
	config.allow_light_mapper_bot_full_perms = XMLParseUtils::parseBoolWithDefault(root_elem, "allow_light_mapper_bot_full_perms", /*default val=*/false);
	config.update_parcel_sales			= XMLParseUtils::parseBoolWithDefault(root_elem, "update_parcel_sales", /*default val=*/false);
	return config;
}


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::init();
	PlatformUtils::ignoreUnixSignals();
	TLSSocket::initTLS();

	conPrint("Substrata server v" + ::cyberspace_version);

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--enable_dev_mode"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--save_sanitised_database"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg
		syntax["--db_path"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg: path to database file on disk

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax, /*allow_unnamed_arg=*/false);

		const bool dev_mode = parsed_args.isArgPresent("--enable_dev_mode");

		Server server;

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
			ServerTestSuite::test();
			return 0;
		}
		//-----------------------------------------------------------------------------------------


		const int listen_port = 7600; // Listen port for sub protocol

#if defined(_WIN32)
		const std::string substrata_appdata_dir = PlatformUtils::getOrCreateAppDataDirectory("Substrata");
		const std::string server_state_dir = substrata_appdata_dir + "/server_data";
#elif defined(OSX)
		const std::string username = PlatformUtils::getLoggedInUserName();
		const std::string server_state_dir = "/Users/" + username + "/cyberspace_server_state";
#else
		const std::string username = PlatformUtils::getLoggedInUserName();
		const std::string server_state_dir = "/home/" + username + "/cyberspace_server_state";
#endif
		conPrint("server_state_dir: " + server_state_dir);
		FileUtils::createDirIfDoesNotExist(server_state_dir);


		// Parse server config, if present:
		ServerConfig server_config;
		{
			const std::string config_path = server_state_dir + "/substrata_server_config.xml";
			if(FileUtils::fileExists(config_path))
			{
				conPrint("Parsing server config from '" + config_path + "'...");
				server_config = parseServerConfig(config_path);
			}
			else
				conPrint("server config not found at '" + config_path + "', using default configuration values instead.");
		}

		server.config = server_config;

		// Parse server credentials
		try
		{
			const ServerCredentials server_credentials = parseServerCredentials(server_state_dir);
			server.world_state->server_credentials = server_credentials;
		}
		catch(glare::Exception& e)
		{
			conPrint("WARNING: Error while loading server credentials: " + e.what());
		}


		const std::string server_resource_dir = server_state_dir + "/server_resources";
		FileUtils::createDirIfDoesNotExist(server_resource_dir);

		server.world_state->resource_manager = new ResourceManager(server_resource_dir);


		// Copy default avatar model into resource dir
		{
			const std::string mesh_URL = "xbot_glb_3242545562312850498.bmesh";

			if(!server.world_state->resource_manager->isFileForURLPresent(mesh_URL))
			{
				const std::string src_path = server_state_dir + "/dist_resources/" + mesh_URL;
				if(FileUtils::fileExists(src_path))
					server.world_state->resource_manager->copyLocalFileToResourceDir(src_path, mesh_URL);
				else
					conPrint("WARNING: file '" + src_path + "' did not exist, default avatar model will be missing for webclient users.");
			}
		}


		// Reads database at the path given by arg 0, writes a sanitised and compacted database at arg 0 path, with "_sanitised" appended to filename.
		if(parsed_args.isArgPresent("--save_sanitised_database"))
		{
			const std::string src_db_path = parsed_args.getArgStringValue("--save_sanitised_database");
			const std::string sanitised_db_path = ::removeDotAndExtension(src_db_path) + "_sanitised.bin";

			// Copy database from src database path to sanitised path.
			FileUtils::copyFile(/*src=*/src_db_path, /*dest=*/sanitised_db_path);

			server.world_state->readFromDisk(sanitised_db_path);

			server.world_state->saveSanitisedDatabase();

			server.world_state = NULL; // Close database

			Database db;
			db.removeOldRecordsOnDisk(sanitised_db_path); // Remove deleted and old records from the database file.
			return 0;
		}


#if defined(_WIN32) || defined(OSX)
		server.screenshot_dir = server_state_dir + "/screenshots"; // Dir generated screenshots will be saved to.
#else
		server.screenshot_dir = "/var/www/cyberspace/screenshots";
#endif
		FileUtils::createDirIfDoesNotExist(server.screenshot_dir);

		std::string server_state_path;
		if(parsed_args.isArgPresent("--db_path"))
			server_state_path = parsed_args.getArgStringValue("--db_path");
		else
			server_state_path = server_state_dir + "/server_state.bin"; // If --db_path is not on command line, use default path.

		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);
		else
			server.world_state->createNewDatabase(server_state_path);


		WorldCreation::createParcelsAndRoads(server.world_state);

		// WorldCreation::removeHypercardMaterials(*server.world_state);

		updateMapTiles(*server.world_state);

		// updateToUseImageCubeMeshes(*server.world_state);
		
		server.world_state->denormaliseData();

		// If there are explicit paths to cert file and private key file in server config, use them, otherwise use default paths.
		std::string tls_certificate_path, tls_private_key_path;
		if(!server_config.tls_certificate_path.empty())
		{
			tls_certificate_path = server_config.tls_certificate_path;
			tls_private_key_path = server_config.tls_private_key_path;
		}
		else
		{
			tls_certificate_path = server_state_dir + "/MyCertificate.crt"; // Use some default paths
			tls_private_key_path = server_state_dir + "/MyKey.key";

			// See https://substrata.info/running_your_own_server , 'Generating a TLS keypair'.
		}

		conPrint("tls_certificate_path: " + tls_certificate_path);
		conPrint("tls_private_key_path: " + tls_private_key_path);
		
		if(!FileUtils::fileExists(tls_certificate_path))
			throw glare::Exception("ERROR: No file found at TLS certificate path '" + tls_certificate_path + "'");
		if(!FileUtils::fileExists(tls_private_key_path))
			throw glare::Exception("ERROR: No file found at TLS private key path '" + tls_private_key_path + "'");


		//----------------------------------------------- Launch webserver -----------------------------------------------
		// Create TLS configuration
		struct tls_config* web_tls_configuration = tls_config_new();

		if(tls_config_set_cert_file(web_tls_configuration, tls_certificate_path.c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));

		if(tls_config_set_key_file(web_tls_configuration, tls_private_key_path.c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));

		Reference<WebDataStore> web_data_store = new WebDataStore();

		std::string default_fragments_dir, default_webclient_dir, default_webserver_public_files_dir;
#if defined(_WIN32) || defined(OSX)
		default_fragments_dir				= server_state_dir + "/webserver_fragments";
		default_webserver_public_files_dir	= server_state_dir + "/webserver_public_files";
		default_webclient_dir				= server_state_dir + "/webclient";
#else
		default_fragments_dir				= "/var/www/cyberspace/webserver_fragments";
		default_webserver_public_files_dir	= "/var/www/cyberspace/public_html";
		default_webclient_dir				= "/var/www/cyberspace/webclient";
		//web_data_store->letsencrypt_webroot			= "/var/www/cyberspace/letsencrypt_webroot";
#endif
		// Use fragments_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webserver_fragments_dir.empty())
			web_data_store->fragments_dir = server_config.webserver_fragments_dir;
		else
			web_data_store->fragments_dir = default_fragments_dir;

		// Use webserver_public_files_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webserver_public_files_dir.empty())
			web_data_store->public_files_dir = server_config.webserver_public_files_dir;
		else
			web_data_store->public_files_dir = default_webserver_public_files_dir;

		// Use webclient_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webclient_dir.empty())
			web_data_store->webclient_dir = server_config.webclient_dir;
		else
			web_data_store->webclient_dir = default_webclient_dir;

		conPrint("webserver fragments_dir: " + web_data_store->fragments_dir);
		conPrint("webserver public_files_dir: " + web_data_store->public_files_dir);
		conPrint("webserver webclient_dir: " + web_data_store->webclient_dir);

		FileUtils::createDirIfDoesNotExist(web_data_store->fragments_dir);
		FileUtils::createDirIfDoesNotExist(web_data_store->public_files_dir);
		FileUtils::createDirIfDoesNotExist(web_data_store->webclient_dir);

		web_data_store->loadAndCompressFiles();

		Reference<WebServerSharedRequestHandler> shared_request_handler = new WebServerSharedRequestHandler();
		shared_request_handler->data_store = web_data_store.ptr();
		shared_request_handler->server = &server;
		shared_request_handler->world_state = server.world_state.ptr();
		shared_request_handler->dev_mode = dev_mode;

		ThreadManager web_thread_manager;
		web_thread_manager.addThread(new web::WebListenerThread(80,  shared_request_handler.getPointer(), NULL));
		web_thread_manager.addThread(new web::WebListenerThread(443, shared_request_handler.getPointer(), web_tls_configuration));


		web_thread_manager.addThread(new WebDataFileWatcherThread(web_data_store));

		//----------------------------------------------- End launch webserver -----------------------------------------------


		// While Coinbase webhooks are not working, add a Coinbase polling thread.
#if USE_GLARE_PARCEL_AUCTION_CODE
		if(!dev_mode)
			web_thread_manager.addThread(new CoinbasePollerThread(server.world_state.ptr()));

		if(!dev_mode)
			web_thread_manager.addThread(new OpenSeaPollerThread(server.world_state.ptr()));
#endif


		//----------------------------------------------- Launch Substrata protocol server -----------------------------------------------
		// Create TLS configuration for substrata protocol server
		struct tls_config* tls_configuration = tls_config_new();

		if(tls_config_set_cert_file(tls_configuration, tls_certificate_path.c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(tls_configuration));
		
		if(tls_config_set_key_file(tls_configuration, tls_private_key_path.c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(tls_configuration));

		conPrint("Launching ListenerThread...");

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server, tls_configuration));
		
		conPrint("Done.");
		//----------------------------------------------- End launch substrata protocol server -----------------------------------------------

		server.mesh_lod_gen_thread_manager.addThread(new MeshLODGenThread(server.world_state.ptr()));

		//thread_manager.addThread(new ChunkGenThread(server.world_state.ptr()));

		server.udp_handler_thread_manager.addThread(new UDPHandlerThread(&server));

		server.dyn_tex_updater_thread_manager.addThread(new DynamicTextureUpdaterThread(&server, server.world_state.ptr()));

		Timer save_state_timer;

		// A map from world name to a vector of packets to send to clients connected to that world.
		std::map<std::string, std::vector<std::string>> broadcast_packets;

		// Main server loop
		uint64 loop_iter = 0;
		while(1)
		{
			PlatformUtils::Sleep(100);

			SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);

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
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
								writeAvatarToNetworkStream(*avatar, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->other_dirty = false;
								avatar->transform_dirty = false;
								i++;
							}
							else if(avatar->state == Avatar::State_JustCreated)
							{
								// Send AvatarCreated packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarCreated);
								writeAvatarToNetworkStream(*avatar, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->state = Avatar::State_Alive;
								avatar->other_dirty = false;
								avatar->transform_dirty = false;

								i++;
							}
							else if(avatar->state == Avatar::State_Dead)
							{
								// Send AvatarDestroyed packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarDestroyed);
								writeToStream(avatar->uid, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

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
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarTransformUpdate);
								writeToStream(avatar->uid, scratch_packet);
								writeToStream(avatar->pos, scratch_packet);
								writeToStream(avatar->rotation, scratch_packet);
								scratch_packet.writeUInt32(avatar->anim_state);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

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
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectFullUpdate);
								ob->writeToNetworkStream(scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_other_dirty = false;
								ob->from_remote_transform_dirty = false; // transform is sent in full packet also.
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_JustCreated)
							{
								// Send ObjectCreated packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectCreated);
								ob->writeToNetworkStream(scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->state = WorldObject::State_Alive;
								ob->from_remote_other_dirty = false;
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_Dead)
							{
								// Send ObjectDestroyed packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectDestroyed);
								writeToStream(ob->uid, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								// Remove from dirty-set, so it's not updated in DB.
								world_state->db_dirty_world_objects.erase(ob);

								// Add DB record to list of records to be deleted.
								server.world_state->db_records_to_delete.insert(ob->database_key);

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
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectTransformUpdate);
								writeToStream(ob->uid, scratch_packet);
								writeToStream(ob->pos, scratch_packet);
								writeToStream(ob->axis, scratch_packet);
								scratch_packet.writeFloat(ob->angle);
								writeToStream(ob->scale, scratch_packet);

								scratch_packet.writeUInt32(ob->last_transform_update_avatar_uid);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_transform_dirty = false;
								server.world_state->markAsChanged();
							}
						}
						else if(ob->from_remote_physics_transform_dirty)
						{
							//conPrint("Object 'physics transform' dirty, sending physics transform update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectPhysicsTransformUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsTransformUpdate);
								writeToStream(ob->uid, scratch_packet);
								writeToStream(ob->pos, scratch_packet);

								const Quatf rot = Quatf::fromAxisAndAngle(ob->axis, ob->angle);
								scratch_packet.writeData(&rot.v.x, sizeof(float) * 4);

								scratch_packet.writeData(ob->linear_vel.x, sizeof(float) * 3);
								scratch_packet.writeData(ob->angular_vel.x, sizeof(float) * 3);

								scratch_packet.writeUInt32(ob->last_transform_update_avatar_uid);
								scratch_packet.writeDouble(ob->last_transform_client_time);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_transform_dirty = false;
								server.world_state->markAsChanged();
							}
						}
						else if(ob->from_remote_lightmap_url_dirty)
						{
							// Send ObjectLightmapURLChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectLightmapURLChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->lightmap_url);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_lightmap_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_model_url_dirty)
						{
							// Send ObjectModelURLChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectModelURLChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->model_url);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_model_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_flags_dirty)
						{
							// Send ObjectFlagsChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectFlagsChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeUInt32(ob->flags);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_flags_dirty = false;
							server.world_state->markAsChanged();
						}

					}

					world_state->dirty_from_remote_objects.clear();
				} // End for each server world


				if(server.world_state->server_admin_message_changed)
				{
					conPrint("Sending ServerAdminMessages to clients...");

					// Send out ServerAdminMessageID packets to clients
					MessageUtils::initPacket(scratch_packet, Protocol::ServerAdminMessageID);
					scratch_packet.writeStringLengthFirst(server.world_state->server_admin_message);
					MessageUtils::updatePacketLengthField(scratch_packet);

					Lock lock3(server.worker_thread_manager.getMutex());
					for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
					{
						assert(dynamic_cast<WorkerThread*>(i->getPointer()));
						static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(scratch_packet);
					}

					server.world_state->server_admin_message_changed = false;
				}

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

			// Clear broadcast_packets vectors of packets.
			for(auto it = broadcast_packets.begin(); it != broadcast_packets.end(); ++it)
				it->second.clear();
			
			if((loop_iter % 40) == 0) // Approx every 4 s.
			{
				// Send out TimeSyncMessage packets to clients
				MessageUtils::initPacket(scratch_packet, Protocol::TimeSyncMessage);
				scratch_packet.writeDouble(server.getCurrentGlobalTime());
				MessageUtils::updatePacketLengthField(scratch_packet);

				Lock lock3(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					assert(dynamic_cast<WorkerThread*>(i->getPointer()));
					static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(scratch_packet);
				}
			}

#if USE_GLARE_PARCEL_AUCTION_CODE
			if(server_config.update_parcel_sales && ((loop_iter % 512) == 0)) // Approx every 50 s.
			{
				AuctionManagement::updateParcelSales(*server.world_state);

				// Want want to list new parcels (to bring the total number being listed up to our target number) every day at midnight UTC.
				/*int hour, day, year;
				Clock::getHourDayOfYearAndYear(Clock::getSecsSince1970(), hour, day, year);
				
				const bool different_day = 
					server.world_state->last_parcel_update_info.last_parcel_sale_update_year != year ||
					server.world_state->last_parcel_update_info.last_parcel_sale_update_day != day;
				
				const bool initial_listing = server.world_state->last_parcel_update_info.last_parcel_sale_update_year == 0;
				
				if(initial_listing || different_day)
				{
					updateParcelSales(*server.world_state);
					server.world_state->last_parcel_update_info.last_parcel_sale_update_hour = hour;
					server.world_state->last_parcel_update_info.last_parcel_sale_update_day = day;
					server.world_state->last_parcel_update_info.last_parcel_sale_update_year = year;
					
					server.world_state->last_parcel_update_info.db_dirty = true; // Save to DB
					server.world_state->markAsChanged();
				}*/
			}
#endif

			if(server.world_state->hasChanged() && (save_state_timer.elapsed() > 10.0))
			{
				try
				{
					// Save world state to disk
					Lock lock2(server.world_state->mutex);

					server.world_state->serialiseToDisk();

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
		stdErrPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(glare::Exception& e)
	{
		stdErrPrint("glare::Exception: " + e.what());
		return 1;
	}

	Networking::shutdown();
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


void Server::clientUDPPortOpen(WorkerThread* worker_thread, const IPAddress& ip_addr, UID client_avatar_id)
{
	conPrint("Server::clientUDPPortOpen(): worker_thread: 0x" + toHexString((uint64)worker_thread) + ", ip_addr: " + ip_addr.toString());// + ", port: " + toString(client_UDP_port));

	{
		Lock lock(connected_clients_mutex);
		if(connected_clients.count(worker_thread) == 0)
		{
			connected_clients.insert(std::make_pair(worker_thread, 
				ServerConnectedClientInfo({ip_addr, client_avatar_id, /*client_UDP_port=*/-1})));
			connected_clients_changed = 1;
		}
	}
}


void Server::clientUDPPortBecameKnown(UID client_avatar_uid, const IPAddress& ip_addr, int client_UDP_port)
{
	bool change_made = false;
	{
		Lock lock(connected_clients_mutex);
		for(auto it = connected_clients.begin(); it != connected_clients.end(); ++it)
		{
			ServerConnectedClientInfo& info = it->second;
			if(info.client_avatar_id == client_avatar_uid)
			{
				if(it->second.client_UDP_port != client_UDP_port)
				{
					it->second.client_UDP_port = client_UDP_port;
					change_made = true;
					
				}
			}
		}
	}

	if(change_made)
	{
		connected_clients_changed = 1;

		conPrint("Server::clientUDPPortBecameKnown(): client with client_avatar_uid " + client_avatar_uid.toString() + ", ip_addr: " + ip_addr.toString() + ", has port: " + toString(client_UDP_port));
	}
}


void Server::clientDisconnected(WorkerThread* worker_thread)
{
	conPrint("Server::clientDisconnected(): worker_thread: 0x" + toHexString((uint64)worker_thread));

	{
		Lock lock(connected_clients_mutex);
		connected_clients.erase(worker_thread);
		connected_clients_changed = 1;
	}
}
