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
#include "ChunkGenThread.h"
#include "WorkerThread.h"
#include "ServerTestSuite.h"
#include "WorldCreation.h"
#include "LuaHTTPRequestManager.h"
#include "WorldMaintenance.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../shared/MessageUtils.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/WorldStateLock.h"
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
#include <graphics/BasisDecoder.h>
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
#if !defined(_WIN32)
#include <signal.h>
#endif


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
	config.webserver_fragments_dir				= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_fragments_dir", /*default val=*/"");
	config.webserver_public_files_dir			= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_public_files_dir", /*default val=*/"");
	config.webclient_dir						= XMLParseUtils::parseStringWithDefault(root_elem, "webclient_dir", /*default val=*/"");
	config.tls_certificate_path					= XMLParseUtils::parseStringWithDefault(root_elem, "tls_certificate_path", /*default val=*/"");
	config.tls_private_key_path					= XMLParseUtils::parseStringWithDefault(root_elem, "tls_private_key_path", /*default val=*/"");
	config.allow_light_mapper_bot_full_perms	= XMLParseUtils::parseBoolWithDefault(root_elem, "allow_light_mapper_bot_full_perms", /*default val=*/false);
	config.update_parcel_sales					= XMLParseUtils::parseBoolWithDefault(root_elem, "update_parcel_sales", /*default val=*/false);
	config.do_lua_http_request_rate_limiting	= XMLParseUtils::parseBoolWithDefault(root_elem, "do_lua_http_request_rate_limiting", /*default val=*/true);
	config.enable_LOD_chunking					= XMLParseUtils::parseBoolWithDefault(root_elem, "enable_LOD_chunking", /*default val=*/true);
	return config;
}


static bool isFeatureFlagSet(Reference<ServerAllWorldsState>& worlds_state, uint64 flag)
{
	WorldStateLock lock(worlds_state->mutex);
	return BitUtils::isBitSet(worlds_state->feature_flag_info.feature_flags, flag);
}


static glare::AtomicInt should_quit(0);


#if !defined(_WIN32)
static void signalHandler(int signal)
{
	assert(signal == SIGTERM || signal == SIGINT);
	should_quit = 1;
}
#endif


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::init();
	PlatformUtils::ignoreUnixSignals();
	TLSSocket::initTLS();
	BasisDecoder::init();

	// Listen for SIGTERM and SIGINT on Linux and Mac.
	// Upon receiving SIGTERM or SIGINT, save dirty data to database, then try and shut down gracefully.
#if !defined(_WIN32)
	// Set a signal handler for SIGTERM
	struct sigaction act;
	act.sa_handler = signalHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if(sigaction(SIGTERM, &act, /*oldact=*/nullptr) != 0)
		fatalError("Failed to set SIGTERM signal handler: " + PlatformUtils::getLastErrorString());
	if(sigaction(SIGINT, &act, /*oldact=*/nullptr) != 0)
		fatalError("Failed to set SIGINT signal handler: " + PlatformUtils::getLastErrorString());
#endif

	conPrint("Substrata server v" + ::cyberspace_version);

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--enable_dev_mode"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--save_sanitised_database"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg
		syntax["--db_path"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg: path to database file on disk
		syntax["--do_not_load_resources"] = std::vector<ArgumentParser::ArgumentType>();

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
#else
		const std::string home_dir = PlatformUtils::getEnvironmentVariable("HOME");
		const std::string server_state_dir = home_dir + "/cyberspace_server_state";
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
			const URLString mesh_URL = "xbot_glb_3242545562312850498.bmesh";

			if(!server.world_state->resource_manager->isFileForURLPresent(mesh_URL))
			{
				const std::string src_path = server_state_dir + "/dist_resources/" + toStdString(mesh_URL);
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

#if defined(_WIN32) || defined(OSX)
		server.photo_dir = server_state_dir + "/photos"; // Dir uploaded photos will be saved to.
#else
		server.photo_dir = "/var/www/cyberspace/photos";
#endif
		FileUtils::createDirIfDoesNotExist(server.photo_dir);

		std::string server_state_path;
		if(parsed_args.isArgPresent("--db_path"))
			server_state_path = parsed_args.getArgStringValue("--db_path");
		else
			server_state_path = server_state_dir + "/server_state.bin"; // If --db_path is not on command line, use default path.

		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);
		else
			server.world_state->createNewDatabase(server_state_path);

		if(parsed_args.isArgPresent("--do_not_load_resources"))
		{
			Lock lock(server.world_state->resource_manager->getMutex());
			server.world_state->resource_manager->getResourcesForURL().clear();
		}


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
		server.world_state->web_data_store = web_data_store.ptr();

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

		web_data_store->screenshot_dir = server.screenshot_dir;
		web_data_store->photo_dir = server.photo_dir;

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

		server.mesh_lod_gen_thread_manager.addThread(new MeshLODGenThread(&server, server.world_state.ptr()));

		if(server_config.enable_LOD_chunking)
			thread_manager.addThread(new ChunkGenThread(server.world_state.ptr()));

		server.udp_handler_thread_manager.addThread(new UDPHandlerThread(&server));

		server.dyn_tex_updater_thread_manager.addThread(new DynamicTextureUpdaterThread(&server, server.world_state.ptr()));

		server.lua_http_manager = new LuaHTTPRequestManager(&server);

		//----------------------------------------------- Create any Lua scripts for objects -----------------------------------------------
		if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG))
		{ // Begin scope for world_state->mutex lock
			conPrint("Creating Lua scripts for objects...");

			WorldStateLock lock(server.world_state->mutex);
			for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
			{
				Reference<ServerWorldState> world_state = world_it->second;
				ServerWorldState::ObjectMapType& objects = world_state->getObjects(lock);
				for(auto i = objects.begin(); i != objects.end(); ++i)
				{
					WorldObject* ob = i->second.ptr();
					if(hasPrefix(ob->script, "--lua") && ob->creator_id.valid())
					{
						try
						{
							runtimeCheck(ob->creator_id.valid()); // Invalid UserID is the empty key so make sure is valid.

							// Get the SubstrataLuaVM for the user who created the object, or create it if it does not yet exist.
							SubstrataLuaVM* lua_vm;
							auto res = server.world_state->lua_vms.find(ob->creator_id);
							if(res == server.world_state->lua_vms.end())
							{
								conPrint("Creating new SubstrataLuaVM for user " + ob->creator_id.toString() + "...");
								Timer timer;
								lua_vm = new SubstrataLuaVM(SubstrataLuaVM::SubstrataLuaVMArgs(&server));
								conPrint("\tCreating SubstrataLuaVM took " + timer.elapsedStringMSWIthNSigFigs(4));
								server.world_state->lua_vms[ob->creator_id] = lua_vm;
							}
							else
								lua_vm = res->second.ptr();

							runtimeCheck(lua_vm);
							ob->lua_script_evaluator = new LuaScriptEvaluator(lua_vm, /*script output handler=*/&server, ob->script, ob, world_state.ptr(), lock);
						}
						catch(LuaScriptExcepWithLocation& e)
						{
							conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.messageWithLocations());
							server.logLuaError("Error: " + e.messageWithLocations(), ob->uid, ob->creator_id);
						}
						catch(glare::Exception& e)
						{
							conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.what());
							server.logLuaError("Error: " + e.what(), ob->uid, ob->creator_id);
						}
					}
				}
			}
		}
		else
			conPrint("Not creating any Lua scripts for objects, server-side script execution is disabled.");
		//----------------------------------------------- End create any Lua scripts for objects -----------------------------------------------

		Timer save_state_timer;

		// A map from world name to a vector of packets to send to clients connected to that world.
		std::map<std::string, std::vector<std::string>> broadcast_packets;

		SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		js::Vector<ThreadMessageRef, 16> temp_thread_messages;

		// Main server loop
		uint64 loop_iter = 0;
		while(!should_quit)
		{
			PlatformUtils::Sleep(100);

			// Do Lua timer callbacks
			if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG))
			{
				{
					WorldStateLock lock(server.world_state->mutex);

					const double cur_time = server.total_timer.elapsed();
					server.timer_queue.update(cur_time, /*triggered_timers_out=*/server.temp_triggered_timers);

					for(size_t i=0; i<server.temp_triggered_timers.size(); ++i)
					{
						TimerQueueTimer& timer = server.temp_triggered_timers[i];
			
						LuaScriptEvaluator* script_evaluator = timer.lua_script_evaluator.getPtrIfAlive();
						if(script_evaluator)
						{
							// Check timer is still valid (has not been destroyed by destroyTimer), by checking the timer id with the same index is still equal to our timer id.
							assert(timer.timer_index >= 0 && timer.timer_index <= LuaScriptEvaluator::MAX_NUM_TIMERS);
							if(timer.timer_id == script_evaluator->timers[timer.timer_index].id)
							{
								script_evaluator->doOnTimerEvent(timer.onTimerEvent_ref, lock); // Execute the Lua timer event callback function

								if(timer.repeating)
								{
									// Re-insert timer with updated trigger time
									timer.tigger_time = cur_time + timer.period;
									server.timer_queue.addTimer(cur_time, timer);
								}
								else // Else if timer was a one-shot timer, 'destroy' it.
								{
									script_evaluator->destroyTimer(timer.timer_index);
								}
							}
						}
					}
				}

				if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG))
					server.lua_http_manager->think();
			}
			
			// Handle any queued messages from worker threads
			{
				server.message_queue.dequeueAnyQueuedItems(temp_thread_messages);

				for(size_t msg_i=0; msg_i<temp_thread_messages.size(); ++msg_i)
				{
					Reference<ThreadMessage> msg = temp_thread_messages[msg_i];

					if(dynamic_cast<UserUsedObjectThreadMessage*>(msg.ptr()))
					{
						const UserUsedObjectThreadMessage* used_msg = static_cast<UserUsedObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex); // Just hold the world state lock while executing script event handlers for now.
						auto res = used_msg->world->getObjects(world_lock).find(used_msg->object_uid);
						if(res != used_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute doOnUserUsedObject event handler in any scripts that are listening for onUserUsedObject for this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserUsedObjectHandlers(/*avatar_uid=*/used_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserTouchedObjectThreadMessage*>(msg.ptr()))
					{
						const UserTouchedObjectThreadMessage* touched_msg = static_cast<UserTouchedObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = touched_msg->world->getObjects(world_lock).find(touched_msg->object_uid);
						if(res != touched_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute doOnUserTouchedObject event handler in any scripts that are listening for onUserTouchedObject for this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserTouchedObjectHandlers(touched_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserMovedNearToObjectThreadMessage*>(msg.ptr()))
					{
						const UserMovedNearToObjectThreadMessage* moved_msg = static_cast<UserMovedNearToObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = moved_msg->world->getObjects(world_lock).find(moved_msg->object_uid);
						if(res != moved_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute onUserMovedNearToObject event handler in any scripts that are listening for onUserMovedNearToObject for this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserMovedNearToObjectHandlers(moved_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserMovedAwayFromObjectThreadMessage*>(msg.ptr()))
					{
						const UserMovedAwayFromObjectThreadMessage* moved_msg = static_cast<UserMovedAwayFromObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = moved_msg->world->getObjects(world_lock).find(moved_msg->object_uid);
						if(res != moved_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute event handler in any scripts that are listening on this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserMovedAwayFromObjectHandlers(moved_msg->avatar_uid, moved_msg->object_uid, world_lock);
						}
					}
					else if(dynamic_cast<UserEnteredParcelThreadMessage*>(msg.ptr()))
					{
						const UserEnteredParcelThreadMessage* parcel_msg = static_cast<UserEnteredParcelThreadMessage*>(msg.ptr());

						if(parcel_msg->object_uid.valid())
						{
							// Look up object
							WorldStateLock world_lock(server.world_state->mutex);
							auto res = parcel_msg->world->getObjects(world_lock).find(parcel_msg->object_uid);
							if(res != parcel_msg->world->getObjects(world_lock).end())
							{
								WorldObject* ob = res->second.ptr();

								// Execute event handler in any scripts that are listening on this object
								if(ob->event_handlers)
									ob->event_handlers->executeOnUserEnteredParcelHandlers(parcel_msg->avatar_uid, parcel_msg->object_uid, parcel_msg->parcel_id, world_lock);
							}
						}
						else
						{
							// If object_uid is invalid, then this event is not from a script, but just from a user entering a parcel.
							// See if there are any social events currently happening on this parcel, if there are, add user to attendee list.
							if(parcel_msg->client_user_id.valid())
							{
								const TimeStamp current_time = TimeStamp::currentTime();

								WorldStateLock world_lock(server.world_state->mutex);
								for(auto it = server.world_state->events.begin(); it != server.world_state->events.end(); ++it)
								{
									SubEvent* event = it->second.ptr();
									if((event->parcel_id == parcel_msg->parcel_id) && // If event is at this parcel
										(event->start_time <= current_time) && // and is currently happening
										(event->end_time >= current_time))
									{
										// Add the client to the event attendee list (if not already inserted)
										const bool inserted = event->attendee_ids.insert(parcel_msg->client_user_id).second;
										if(inserted)
											server.world_state->addEventAsDBDirty(event);
									}
								}
							}
						}
					}
					else if(dynamic_cast<UserExitedParcelThreadMessage*>(msg.ptr()))
					{
						const UserExitedParcelThreadMessage* parcel_msg = static_cast<UserExitedParcelThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = parcel_msg->world->getObjects(world_lock).find(parcel_msg->object_uid);
						if(res != parcel_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute event handler in any scripts that are listening on this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserExitedParcelHandlers(parcel_msg->avatar_uid, parcel_msg->object_uid, parcel_msg->parcel_id, world_lock);
						}
					}
					else if(NewResourceGenerated* gen_msg = dynamic_cast<NewResourceGenerated*>(msg.ptr()))
					{
						// Send NewResourceOnServer message to connected clients
						{
							MessageUtils::initPacket(scratch_packet, Protocol::NewResourceOnServer);
							scratch_packet.writeStringLengthFirst(gen_msg->URL);
							MessageUtils::updatePacketLengthField(scratch_packet);

							Lock lock3(server.worker_thread_manager.getMutex());
							for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
							{
								assert(dynamic_cast<WorkerThread*>(i->getPointer()));
								static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(scratch_packet);
							}
						}
					}
				}
			}

			{ // Begin scope for world_state->mutex lock

				WorldStateLock lock(server.world_state->mutex);

				for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
				{
					Reference<ServerWorldState> world_state = world_it->second;

					std::vector<std::string>& world_packets = broadcast_packets[world_it->first];

					// Generate packets for avatar changes
					const ServerWorldState::AvatarMapType& avatars = world_state->getAvatars(lock);
					for(auto i = avatars.begin(); i != avatars.end();)
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
								world_state->getAvatars(lock).erase(old_avatar_iterator);

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
					ServerWorldState::DirtyFromRemoteObjectSetType& dirty_from_remote_objects = world_state->getDirtyFromRemoteObjects(lock);
					for(auto i = dirty_from_remote_objects.begin(); i != dirty_from_remote_objects.end(); ++i)
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
								world_state->getDBDirtyWorldObjects(lock).erase(ob);

								// Add DB record to list of records to be deleted.
								server.world_state->db_records_to_delete.insert(ob->database_key);

								// Remove ob from object map
								world_state->getObjects(lock).erase(ob->uid);

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
						else if(ob->from_remote_content_dirty)
						{
							// Send ObjectContentChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectContentChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->content);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_content_dirty = false;
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

					dirty_from_remote_objects.clear();
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
			if((loop_iter % 1024) == 64) // Approx every 100 s.
				if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG))
					WorldMaintenance::removeOldVehicles(server.world_state);

			if(server.world_state->hasChanged() && (save_state_timer.elapsed() > 10.0))
			{
				try
				{
					// Save world state to disk
					WorldStateLock lock(server.world_state->mutex);

					server.world_state->serialiseToDisk(lock);

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

			//if(loop_iter > 100) // TEMP: test shutting down
			//	break;
		} // End of main server loop

		conPrint("Closing...");

		// Save world state to disk before terminating.
		conPrint("Saving world state to disk before program quits...");
		try
		{
			// Save world state to disk
			WorldStateLock lock(server.world_state->mutex);

			server.world_state->serialiseToDisk(lock);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: saving world state to disk failed: " + e.what());
		}

		// Shut down threads in reverse order of creation
		conPrint("Stopping threads...");

		thread_manager.killThreadsBlocking();

		web_thread_manager.killThreadsBlocking();
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


Server::~Server()
{
	conPrint("Stopping Server threads...");

	// Stop any threads that may refer to other data members first
	dyn_tex_updater_thread_manager.killThreadsBlocking();
	udp_handler_thread_manager.killThreadsBlocking();
	mesh_lod_gen_thread_manager.killThreadsBlocking();
	worker_thread_manager.killThreadsBlocking();

	lua_http_manager = nullptr;

	message_queue.clear();
	timer_queue.clear();

	world_state = nullptr;
}


void Server::printFromLuaScript(LuaScript* script, const char* s, size_t len)
{
	const std::string message(s, len);

	// conPrint("LUA: " + message);

	// Store log message for user in the per-user script message log.
	
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	logLuaMessage(message, UserScriptLogMessage::MessageType_print, script_evaluator->world_object->uid, script_evaluator->world_object->creator_id);
}


void Server::errorOccurredFromLuaScript(LuaScript* script, const std::string& msg)
{
	conPrint("LUA ERROR: " + msg);

	// Store log message for user in the per-user script message log.

	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	logLuaError("Error: " + msg, script_evaluator->world_object->uid, script_evaluator->world_object->creator_id);
}


void Server::logLuaMessage(const std::string& msg, UserScriptLogMessage::MessageType message_type, UID world_ob_uid, UserID script_creator_user_id)
{
	// Get (and create if needed) the per-user script log for script_creator_user_id
	Reference<UserScriptLog> user_script_log;
	{
		Lock lock(world_state->mutex);
		auto res = world_state->user_script_log.find(script_creator_user_id);
		if(res == world_state->user_script_log.end())
		{
			user_script_log = new UserScriptLog();
			world_state->user_script_log.insert(std::make_pair(script_creator_user_id, user_script_log));
		}
		else
			user_script_log = res->second;
	}

	{
		Lock lock(user_script_log->mutex);

		user_script_log->messages.push_back(UserScriptLogMessage({TimeStamp::currentTime(), message_type, world_ob_uid, msg}));

		const size_t MAX_NUM_PER_USER_LUA_MESSAGES = 1000;

		if(user_script_log->messages.size() > MAX_NUM_PER_USER_LUA_MESSAGES)
			user_script_log->messages.pop_front();
	}
}


void Server::logLuaError(const std::string& msg, UID world_ob_uid, UserID script_creator_user_id)
{
	const std::string full_msg = msg + "\nScript will be disabled.";
	logLuaMessage(full_msg, UserScriptLogMessage::MessageType_error, world_ob_uid, script_creator_user_id);
}


double Server::getCurrentGlobalTime() const
{
	return Clock::getTimeSinceInit();
}


void Server::enqueueMsg(ThreadMessageRef msg)
{
	message_queue.enqueue(msg);
}


void Server::enqueueLuaHTTPRequest(Reference<LuaHTTPRequest> request)
{
	if(lua_http_manager)
		lua_http_manager->enqueueHTTPRequest(request);
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
