/*=====================================================================
Server.cpp
----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "../webserver/WebDataFileWatcherThread.h"
#include "MeshLODGenThread.h"
//#include "ChunkGenThread.h"
#include "WorkerThread.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../shared/MessageUtils.h"
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
#include <FileChecksum.h>
#include <CryptoRNG.h>
#include <XMLParseUtils.h>
#include <IndigoXMLDoc.h>
#include <SHA256.h> //TEMP for testing
#include <DatabaseTests.h> //TEMP for testing
#include <ArgumentParser.h>
#include <SocketBufferOutStream.h>
#include <TLSSocket.h>
#include <PCG32.h>
#include <Matrix4f.h>
#include <Quat.h>
#include <Rect2.h>
#include <OpenSSL.h>
#include <Keccak256.h>
#include <graphics/PNGDecoder.h>
#include <graphics/Map2D.h>
#include <tls.h>
#include <networking/HTTPClient.h>//TEMP for testing
#include "../webserver/WebServerRequestHandler.h"
#include "../webserver/AccountHandlers.h"
#include "../webserver/WebDataStore.h"
#include "../ethereum/Infura.h"
#include <WorkerThreadTests.h>//TEMP for testing
#include <WebListenerThread.h>
#include "../webserver/CoinbasePollerThread.h"
#include "../webserver/OpenSeaPollerThread.h"
#include "../ethereum/RLP.h"//TEMP for testing
#include "../ethereum/Signing.h"//TEMP for testing
//#include <graphics/FormatDecoderGLTF.h>//TEMP for testing


static bool isParcelInCurrentAuction(ServerAllWorldsState& world_state, const Parcel* parcel, TimeStamp now) REQUIRES(world_state.mutex)
{
	for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
	{
		auto res = world_state.parcel_auctions.find(parcel->parcel_auction_ids[i]);
		if(res != world_state.parcel_auctions.end())
		{
			const ParcelAuction* auction = res->second.ptr();
			if(auction->currentlyForSale(now))
				return true;
		}
	}

	return false;
}


static void updateParcelSales(ServerAllWorldsState& world_state)
{
	conPrint("updateParcelSales()");

	PCG32 rng((uint64)Clock::getSecsSince1970() + (uint64)(Clock::getTimeSinceInit() * 1000.0)); // Poor seeding but not too important

	{
		Lock lock(world_state.mutex);

		int num_for_sale = 0;
		TimeStamp now = TimeStamp::currentTime();
		for(auto it = world_state.parcel_auctions.begin(); it != world_state.parcel_auctions.end(); ++it)
		{
			ParcelAuction* auction = it->second.ptr();
			if(auction->currentlyForSale(now))
				num_for_sale++;
		}

		const int target_num_for_sale = 9;
		if(num_for_sale < target_num_for_sale)
		{
			// We have less than the desired number of parcels up for sale, so list some:

			// Get list of sellable parcels
			std::vector<Parcel*> sellable_parcels;
			for(auto pit = world_state.getRootWorldState()->parcels.begin(); pit != world_state.getRootWorldState()->parcels.end(); ++pit)
			{
				Parcel* parcel = pit->second.ptr();
				if((parcel->owner_id == UserID(0)) && (parcel->id.value() >= 90) && // If owned my MrAdmin, and not on the blocks by the central square (so ID >= 90)
					!isParcelInCurrentAuction(world_state, parcel, now) && // And not already in a currently running auction
					(parcel->nft_status == Parcel::NFTStatus_NotNFT) && // And not minted as an NFT (For example like parcels that were auctioned on OpenSea, which may not be claimed yet)
					(!(parcel->id.value() >= 426 && parcel->id.value() < 430)) && // Don't auction off new park parcels (parcels 426...429)
					(!(parcel->id.value() >= 954 && parcel->id.value() <= 955)) // Don't auction of Zombot's parcels
					)
					sellable_parcels.push_back(parcel);
			}

			// Permute parcels (Fisher-Yates shuffle).  NOTE: kinda slow if we have lots of sellable parcels.
			/*for(int i=(int)sellable_parcels.size()-1; i>0; --i)
			{
				const uint32 k = rng.nextUInt((uint32)i + 1);
				mySwap(sellable_parcels[i], sellable_parcels[k]);
			}*/

			const int desired_num_to_add = target_num_for_sale - num_for_sale;
			assert(desired_num_to_add > 0);
			const int num_to_add = myMin(desired_num_to_add, (int)sellable_parcels.size());
			for(int i=0; i<num_to_add; ++i)
			{
				Parcel* parcel = sellable_parcels[i];

				conPrint("updateParcelSales(): Putting parcel " + parcel->id.toString() + " up for auction");

				// Make a parcel auction for this parcel

				const int auction_duration_hours = 48;

				double auction_start_price, auction_end_price;
				if(parcel->id.value() >= 430 && parcel->id.value() <= 726) // If this is a market parcel:
				{
					auction_start_price = 1000;
					auction_end_price = 50;
				}
				else
				{
					auction_start_price = 4000; // EUR
					auction_end_price = 400; // EUR
				}

				//TEMP: scan over all ParcelAuctions and find highest used ID.
				uint32 highest_auction_id = 0;
				for(auto it = world_state.parcel_auctions.begin(); it != world_state.parcel_auctions.end(); ++it)
					highest_auction_id = myMax(highest_auction_id, it->first);

				ParcelAuctionRef auction = new ParcelAuction();
				auction->id = highest_auction_id + 1;
				auction->parcel_id = parcel->id;
				auction->auction_state = ParcelAuction::AuctionState_ForSale;
				auction->auction_start_time  = now;
				auction->auction_end_time    = TimeStamp((uint64)(now.time + auction_duration_hours * 3600 - 60)); // Make the auction end slightly before the regen time, for if parcels hit the reserve price.
				auction->auction_start_price = auction_start_price;
				auction->auction_end_price   = auction_end_price;

				world_state.parcel_auctions[auction->id] = auction;

				// Make new screenshot (request) for parcel auction
				uint64 next_shot_id = world_state.getNextScreenshotUID();

				// Close-in screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);

					world_state.addScreenshotAsDBDirty(shot);
				}
				// Zoomed-out screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);

					world_state.addScreenshotAsDBDirty(shot);
				}

				parcel->parcel_auction_ids.push_back(auction->id);

				world_state.getRootWorldState()->addParcelAsDBDirty(parcel);

				world_state.addParcelAuctionAsDBDirty(auction);
			}

			conPrint("updateParcelSales(): Put " + toString(num_to_add) + " parcels up for auction.");
		}
	} // End lock scope
}


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
#if defined(_WIN32) || defined(OSX)
	const std::string path = server_state_dir + "/substrata_server_credentials.txt";
#else
	const std::string username = PlatformUtils::getLoggedInUserName();
	const std::string path = "/home/" + username + "/substrata_server_credentials.txt";
#endif

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

		creds.creds[key.to_string()] = ::stripHeadAndTailWhitespace(value.to_string());
	}

	return creds;
}


static ServerConfig parseServerConfig(const std::string& config_path)
{
	IndigoXMLDoc doc(config_path);
	pugi::xml_node root_elem = doc.getRootElement();

	ServerConfig config;
	config.webclient_dir = XMLParseUtils::parseStringWithDefault(root_elem, "webclient_dir", /*default val=*/"");
	config.allow_light_mapper_bot_full_perms = XMLParseUtils::parseBoolWithDefault(root_elem, "allow_light_mapper_bot_full_perms", /*default val=*/false);
	return config;
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
		syntax["--enable_dev_mode"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--save_sanitised_database"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax, /*allow_unnamed_arg=*/false);

		const bool dev_mode = parsed_args.isArgPresent("--enable_dev_mode");

		Server server;

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
#if BUILD_TESTS
			//SafeBrowsingCheckerThread::test(server.world_state.ptr());
			//GIFDecoder::test();
			//PNGDecoder::test();
			//BatchedMeshTests::test();
			//glare::BestFitAllocator::test();
			//Parser::doUnitTests();
			//FormatDecoderGLTF::test();
			DatabaseTests::test();
			//StringUtils::test();
			//SHA256::test();
			//RLP::test();
			//Signing::test();
			//Keccak256::test();
			//Infura::test();
			//SHA256::test();
			//RLP::test();
			//Signing::test();
			//Keccak256::test();
			//Infura::test();
			//AccountHandlers::test();
			//web::WorkerThreadTests::test();
			////SHA256::test();
			////CryptoRNG::test();
			//StringUtils::test();
			////HTTPClient::test();
			//Base64::test();
			//Parser::doUnitTests();
			conPrint("----Finished tests----");
#endif
			return 0;
		}
		//-----------------------------------------------------------------------------------------


		const int listen_port = 7600;
		conPrint("listen port: " + toString(listen_port));

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
				conPrint("server config not found at '" + config_path + "', skipping...");
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


		// Reads database at the path given by arg 0, writes a sanitised and compacted database at arg 0 path, with "_sanitised" appended to filename.
		if(parsed_args.isArgPresent("--save_sanitised_database"))
		{
			const std::string src_db_path = parsed_args.getArgStringValue("--save_sanitised_database");
			const std::string sanitised_db_path = ::removeDotAndExtension(src_db_path) + "_sanitised.bin";

			// Copy database from src database path to sanitised path.
			FileUtils::copyFile(/*src=*/src_db_path, /*dest=*/sanitised_db_path);

			server.world_state->readFromDisk(sanitised_db_path, dev_mode);

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


		const std::string server_state_path = server_state_dir + "/server_state.bin";

		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path, dev_mode);
		else
			server.world_state->createNewDatabase(server_state_path);


		updateMapTiles(*server.world_state);

		// updateToUseImageCubeMeshes(*server.world_state);
		
		server.world_state->denormaliseData();

		
		//----------------------------------------------- Launch webserver -----------------------------------------------

		// Create TLS configuration
		struct tls_config* web_tls_configuration = tls_config_new();

#if defined(_WIN32) || defined(OSX)
		// NOTE: key generated with 
		// cd D:\programming\LibreSSL\libressl-2.8.3-x64-vs2019-install\bin
		// ./openssl req -new -newkey rsa:4096 -x509 -sha256 -days 3650 -nodes -out MyCertificate.crt -keyout MyKey.key

		if(tls_config_set_cert_file(web_tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
		if(tls_config_set_key_file(web_tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));

		/*const std::string certdir = "N:\\new_cyberspace\\trunk\\certs\\substrata.info";
		if(tls_config_set_cert_file(web_tls_configuration, (certdir + "/godaddy-1da07c9956c94289.crt").c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
		if(tls_config_set_key_file(web_tls_configuration, (certdir + "/godaddy-generated-private-key.txt").c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));*/

#else
		const std::string certdir = "/home/" + username + "/certs/substrata.info";
		if(FileUtils::fileExists(certdir))
		{
			conPrint("Using godaddy certs");

			if(tls_config_set_cert_file(web_tls_configuration, (certdir + "/godaddy-1da07c9956c94289.crt").c_str()) != 0)
				throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
			if(tls_config_set_key_file(web_tls_configuration, (certdir + "/godaddy-generated-private-key.txt").c_str()) != 0) // set private key
				throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));
		}
		else
		{
			if(FileUtils::fileExists("/etc/letsencrypt/live/substrata.info"))
			{
				conPrint("Using Lets-encrypt certs");

				if(tls_config_set_cert_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
					throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
				if(tls_config_set_key_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
					throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));
			}
			else if(FileUtils::fileExists("/etc/letsencrypt/live/test.substrata.info"))
			{
				conPrint("Using Lets-encrypt certs");

				if(tls_config_set_cert_file(web_tls_configuration, "/etc/letsencrypt/live/test.substrata.info/cert.pem") != 0)
					throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
				if(tls_config_set_key_file(web_tls_configuration, "/etc/letsencrypt/live/test.substrata.info/privkey.pem") != 0) // set private key
					throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));
			}
			else
			{
				if(dev_mode)
				{
					if(tls_config_set_cert_file(web_tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
						throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
					if(tls_config_set_key_file(web_tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
						throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));
				}
				else
				{
					// We need to be able to start without a cert, so we can do the initial Lets-Encrypt challenge
					conPrint("No Lets-encrypt cert dir found, skipping loading cert.");
				}
			}
		}
#endif

		Reference<WebDataStore> web_data_store = new WebDataStore();

		std::string default_webclient_dir;
#if defined(_WIN32)
		if(dev_mode)
		{
			//web_data_store->public_files_dir = FileUtils::getDirectory(PlatformUtils::getFullPathToCurrentExecutable()) + "/webserver_public_files";
			//web_data_store->webclient_dir    = FileUtils::getDirectory(PlatformUtils::getFullPathToCurrentExecutable()) + "/webclient";
			web_data_store->public_files_dir = server_state_dir + "/webserver_public_files";
		}
		else
		{
			web_data_store->public_files_dir = "N:\\new_cyberspace\\trunk\\webserver_public_files";
			web_data_store->letsencrypt_webroot = "C:\\programming\\cyberspace\\webdata\\letsencrypt_webroot";
		}
		default_webclient_dir = server_state_dir + "/webclient";

#elif defined(OSX)
		web_data_store->public_files_dir			= server_state_dir + "/webserver_public_files";
		default_webclient_dir						= server_state_dir + "/webclient";
#else
		web_data_store->public_files_dir			= "/var/www/cyberspace/public_html";
		default_webclient_dir						= "/var/www/cyberspace/webclient";
		web_data_store->letsencrypt_webroot			= "/var/www/cyberspace/letsencrypt_webroot";
#endif
		// Use webclient_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webclient_dir.empty())
			web_data_store->webclient_dir			= server_config.webclient_dir;
		else
			web_data_store->webclient_dir			= default_webclient_dir;

		conPrint("webserver public_files_dir: " + web_data_store->public_files_dir);
		conPrint("webserver webclient_dir: " + web_data_store->webclient_dir);

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
		if(!dev_mode)
			web_thread_manager.addThread(new CoinbasePollerThread(server.world_state.ptr()));

		if(!dev_mode)
			web_thread_manager.addThread(new OpenSeaPollerThread(server.world_state.ptr()));


		//----------------------------------------------- Launch Substrata protocol server -----------------------------------------------
		// Create TLS configuration for substrata protocol server
		struct tls_config* tls_configuration = tls_config_new();

#if defined(_WIN32) || defined(OSX)
		if(tls_config_set_cert_file(tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");
		
		if(tls_config_set_key_file(tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#else
		if(false) // For local testing:
		{
			conPrint("Using MyCertificate.crt etc.");
			
			if(tls_config_set_cert_file(tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
				throw glare::Exception("tls_config_set_cert_file failed.");
		
			if(tls_config_set_key_file(tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
				throw glare::Exception("tls_config_set_key_file failed.");
		}
		else if(FileUtils::fileExists(certdir))
		{
			conPrint("Using godaddy certs");

			if(tls_config_set_cert_file(tls_configuration, (certdir + "/godaddy-1da07c9956c94289.crt").c_str()) != 0)
				throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
			if(tls_config_set_key_file(tls_configuration, (certdir + "/godaddy-generated-private-key.txt").c_str()) != 0) // set private key
				throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));
		}
		else
		{
			if(FileUtils::fileExists("/etc/letsencrypt/live/substrata.info"))
			{
				conPrint("Using Lets-encrypt certs");

				if(tls_config_set_cert_file(tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
					throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(tls_configuration));
				if(tls_config_set_key_file(tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
					throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(tls_configuration));
			}
			else if(FileUtils::fileExists("/etc/letsencrypt/live/test.substrata.info"))
			{
				conPrint("Using Lets-encrypt certs");

				if(tls_config_set_cert_file(tls_configuration, "/etc/letsencrypt/live/test.substrata.info/cert.pem") != 0)
					throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(tls_configuration));
				if(tls_config_set_key_file(tls_configuration, "/etc/letsencrypt/live/test.substrata.info/privkey.pem") != 0) // set private key
					throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(tls_configuration));
			}
			else
			{
				if(dev_mode)
				{
					if(tls_config_set_cert_file(tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
						throw glare::Exception("tls_config_set_cert_file failed.");

					if(tls_config_set_key_file(tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
						throw glare::Exception("tls_config_set_key_file failed.");
				}
			}
		}
#endif
		conPrint("Launching ListenerThread...");

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server, tls_configuration));
		
		conPrint("Done.");
		//----------------------------------------------- End launch substrata protocol server -----------------------------------------------


		if(!dev_mode)
			thread_manager.addThread(new MeshLODGenThread(server.world_state.ptr()));

		//thread_manager.addThread(new ChunkGenThread(server.world_state.ptr()));

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
								writeToNetworkStream(*avatar, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->other_dirty = false;
								avatar->transform_dirty = false;
								i++;
							}
							else if(avatar->state == Avatar::State_JustCreated)
							{
								// Send AvatarCreated packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarCreated);
								writeToNetworkStream(*avatar, scratch_packet);

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

								// Write aabb_data.  Although this was introduced in protocol version 34, clients using an older protocol version should be able to just ignore this data
								// since it is at the end of the message and we use sized messages.
								const float aabb_data[6] = {
									ob->aabb_ws.min_[0], ob->aabb_ws.min_[1], ob->aabb_ws.min_[2],
									ob->aabb_ws.max_[0], ob->aabb_ws.max_[1], ob->aabb_ws.max_[2]
								};
								scratch_packet.writeData(aabb_data, sizeof(float) * 6);

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


			if((loop_iter % 128) == 0) // Approx every 10 s.
			{
				// Want want to list new parcels (to bring the total number being listed up to our target number) every day at midnight UTC.
				int hour, day, year;
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
				}
			}


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
