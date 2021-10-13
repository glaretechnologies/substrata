/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "MeshLODGenThread.h"
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
#include <SHA256.h> //TEMP for testing
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


static void makeRandomParcel(const Vec2d& region_botleft, const Vec2d& region_topright, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state, Map2DRef road_map,
	float base_w, float rng_width, float base_h, float rng_h)
{
	for(int i=0; i<100; ++i)
	{
		const float max_z = base_h + (rng.unitRandom() * rng.unitRandom() * rng.unitRandom() * rng_h);

		const float w = base_w + rng.unitRandom() * rng.unitRandom() * rng_width;
		const float h = base_w + rng.unitRandom() * rng.unitRandom() * rng_width;
		const Vec2d botleft = Vec2d(
			region_botleft.x + rng.unitRandom() * (region_topright.x - region_botleft.x), 
			region_botleft.y + rng.unitRandom() * (region_topright.y - region_botleft.y));

		const Vec2d topright = botleft + Vec2d(w, h);

		const Rect2d bounds(botleft, topright);

		// Invalid if extends out of region
		if(topright.x > region_topright.x || topright.y > region_topright.y)
			continue;
			

		bool valid_parcel = true;
		// Check against road map
		if(road_map.nonNull())
		{
			const int RES = 8;
			for(int x=0; x<RES; ++x)
			for(int y=0; y<RES; ++y)
			{
				// Point in parcel
				const Vec2d p(
					botleft.x + (topright.x - botleft.x) * ((float)x / (RES-1)),
					botleft.y + (topright.y - botleft.y) * ((float)y / (RES-1))
				);

				const Vec2d impos(
					(p.x - region_botleft.x) / (region_topright.x - region_botleft.x),
					(p.y - region_botleft.y) / (region_topright.y - region_botleft.y));
				const float val = road_map->sampleSingleChannelTiled((float)impos.x, (float)impos.y, 0);
				if(val < 0.5f)
				{
					// We are interesecting a road
					valid_parcel = false;
					break;
				}
			}

			if(!valid_parcel)
				continue;
		}
		 
		// Check against existing parcels.
		for(auto it = world_state->parcels.begin(); it != world_state->parcels.end(); ++it)
		{
			const Parcel* p = it->second.ptr();

			const Rect2d other_bounds(Vec2d(p->aabb_min.x, p->aabb_min.y), Vec2d(p->aabb_max.x, p->aabb_max.y));

			if(bounds.intersectsRect2(other_bounds))
			{
				valid_parcel = false;
				break;
			}
		}

		if(valid_parcel)
		{
			const ParcelID parcel_id(next_id++);
			ParcelRef test_parcel = new Parcel();
			test_parcel->state = Parcel::State_Alive;
			test_parcel->id = parcel_id;
			test_parcel->owner_id = UserID(0);
			test_parcel->admin_ids.push_back(UserID(0));
			test_parcel->writer_ids.push_back(UserID(0));
			test_parcel->created_time = TimeStamp::currentTime();
			test_parcel->zbounds = Vec2d(-1, max_z);

			test_parcel->verts[0] = botleft;
			test_parcel->verts[1] = Vec2d(topright.x, botleft.y);
			test_parcel->verts[2] = topright;
			test_parcel->verts[3] = Vec2d(botleft.x, topright.y);

			test_parcel->build();

			world_state->parcels[parcel_id] = test_parcel;
			return;
		}
	}

	conPrint("Reached max iters without finding parcel position.");
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


static void makeRoad(ServerAllWorldsState& world_state, const Vec3d& pos, const Vec3f& scale, float rotation_angle)
{
	WorldObjectRef test_object = new WorldObject();
	test_object->creator_id = UserID(0);
	test_object->state = WorldObject::State_Alive;
	test_object->uid = world_state.getNextObjectUID();//  world_state->UID(road_uid++);
	test_object->pos = pos;
	test_object->angle = rotation_angle;
	test_object->axis = Vec3f(0,0,1);
	test_object->model_url = "Cube_obj_11907297875084081315.bmesh";
	test_object->scale = scale;
	test_object->content = "road";
	test_object->materials.push_back(new WorldMaterial());

	// Set tex matrix based on scale
	test_object->materials[0]->tex_matrix = Matrix2f(scale.x / 10.f, 0, 0, scale.y / 10.f);
	test_object->materials[0]->colour_texture_url = "stone_floor_jpg_6978110256346892991.jpg";

	world_state.getRootWorldState()->objects[test_object->uid] = test_object;
}


static bool isParcelInCurrentAuction(ServerAllWorldsState& world_state, const Parcel* parcel, TimeStamp now)
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
					(!(parcel->id.value() >= 426 && parcel->id.value() < 430)) // Don't auction off new park parcels (parcels 426...429)
					)
					sellable_parcels.push_back(parcel);
			}

			// Permute parcels (Fisher–Yates shuffle).  NOTE: kinda slow if we have lots of sellable parcels.
			for(int i=(int)sellable_parcels.size()-1; i>0; --i)
			{
				const uint32 k = rng.nextUInt((uint32)i + 1);
				mySwap(sellable_parcels[i], sellable_parcels[k]);
			}

			const int desired_num_to_add = target_num_for_sale - num_for_sale;
			assert(desired_num_to_add > 0);
			const int num_to_add = myMin(desired_num_to_add, (int)sellable_parcels.size());
			for(int i=0; i<num_to_add; ++i)
			{
				Parcel* parcel = sellable_parcels[i];

				conPrint("updateParcelSales(): Putting parcel " + parcel->id.toString() + " up for auction");

				// Make a parcel auction for this parcel

				const int auction_duration_hours = 48;
				const double auction_start_price = 4000; // EUR
				const double auction_end_price = 800; // EUR

				//TEMP: scan over all ParcelAuctions and find highest used ID.
				uint32 highest_auction_id = 0;
				for(auto it = world_state.parcel_auctions.begin(); it != world_state.parcel_auctions.end(); ++it)
					highest_auction_id = myMax(highest_auction_id, it->first);

				ParcelAuctionRef auction = new ParcelAuction();
				auction->id = highest_auction_id + 1;
				auction->parcel_id = parcel->id;
				auction->auction_state = ParcelAuction::AuctionState_ForSale;
				auction->auction_start_time  = now;
				auction->auction_end_time    = TimeStamp((uint64)(now.time + auction_duration_hours * 3600));
				auction->auction_start_price = auction_start_price;
				auction->auction_end_price   = auction_end_price;

				world_state.parcel_auctions[auction->id] = auction;

				// Make new screenshot (request) for parcel auction

				//TEMP: scan over all screenshots and find highest used ID. (was running into a problem on localhost of id >= num items)
				uint64 highest_id = 0;
				for(auto it = world_state.screenshots.begin(); it != world_state.screenshots.end(); ++it)
					highest_id = myMax(highest_id, it->first);

				// Close-in screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = highest_id + 1;
					parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);
				}
				// Zoomed-out screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = highest_id + 2;
					parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);
				}

				parcel->parcel_auction_ids.push_back(auction->id);

				world_state.markAsChanged();
			}

			conPrint("updateParcelSales(): Put " + toString(num_to_add) + " parcels up for auction.");
		}
	} // End lock scope
}


void updateMapTiles(ServerAllWorldsState& world_state)
{
	// TEMP: scan over all screenshots and find highest used ID. (was running into a problem on localhost of id >= num items)
	uint64 highest_id = 0;
	for(auto it = world_state.screenshots.begin(); it != world_state.screenshots.end(); ++it)
		highest_id = myMax(highest_id, it->first);

	uint64 next_shot_id = highest_id + 1;

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


			// We want zoom level 3 to have (half) span 2 = 2^1.
			// zoom level 4 : span = 2^(4-2) = 2^2 = 4.
			// So num tiles = (4*2)^2 = 64
			// zoom level 5 : span = 2^(5-2) = 2^3 = 8.
			// So num tiles = (8*2)^2 = 256

			// in general num_tiles = (span*2)^2 = ((2^(z-2))*2)^2 = (2^(z-1))^2 = 2^((z-1)*2) = 2^(2z - 2)
			// zoom level 6: num_tiles = 2^10 = 1024
			//const int span = 1 << myMax(0, z - 2); // 2^(z-2)

			const int x_begin = -span;
			const int x_end = span;
			const int y_begin = -span;
			const int y_end = span;

			

			for(int y = y_begin; y < y_end; ++y)
			for(int x = x_begin; x < x_end; ++x)
			{
				const Vec3<int> v(x, y, z);

				if(world_state.map_tile_info.count(v) == 0)
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

					world_state.map_tile_info[v] = info;

					conPrint("Added map tile screenshot: " + v.toString());

					world_state.markAsChanged();
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


static void enqueuePacketToBroadcast(SocketBufferOutStream& packet_buffer, std::vector<std::string>& broadcast_packets)
{
	if(packet_buffer.buf.size() > 0)
	{
		std::string packet_string(packet_buffer.buf.size(), '\0');

		std::memcpy(&packet_string[0], packet_buffer.buf.data(), packet_buffer.buf.size());

		broadcast_packets.push_back(packet_string);
	}
}


/*static void assignParcelToUser(const Reference<ServerWorldState>& world_state, const ParcelID& parcel_id, const UserID& user_id)
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
}*/


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
			StringUtils::test();
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
		server.screenshot_dir = "C:\\programming\\cyberspace\\webdata\\screenshots"; // Dir generated screenshots will be saved to.
#else
		server.screenshot_dir = "/var/www/cyberspace/screenshots";
#endif
		FileUtils::createDirIfDoesNotExist(server.screenshot_dir);


		const std::string server_state_path = server_state_dir + "/server_state.bin";

		conPrint("server_state_path: " + server_state_path);

		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);


		//TEMP:
		//server.world_state->resource_manager->getResourcesForURL().clear();

		//TEMP
		//server.world_state->parcels.clear();


		updateMapTiles(*server.world_state);
		

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
		if(false)
		{
			for(auto i = server.world_state->getRootWorldState()->parcels.begin(); i != server.world_state->getRootWorldState()->parcels.end(); ++i)
			{
				ParcelRef parcel = i->second;
				parcel->zbounds.y = 10.0f;
			}
		}

		/*
		// Make parcel with id 20 a 'sandbox', world-writeable parcel
		{
			auto res = server.world_state->getRootWorldState()->parcels.find(ParcelID(20));
			if(res != server.world_state->getRootWorldState()->parcels.end())
			{
				res->second->all_writeable = true;
				conPrint("Made parcel 20 all-writeable.");
			}
		}*/


		//server.world_state->objects.clear();

		ParcelID max_parcel_id(0);
		for(auto it = server.world_state->getRootWorldState()->parcels.begin(); it != server.world_state->getRootWorldState()->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();
			max_parcel_id = myMax(max_parcel_id, parcel->id);
		}

		// Add park parcels if not already created.
		if(max_parcel_id.value() == 425)
		{
			for(int i=0; i<4; ++i)
			{
				const ParcelID parcel_id(426 + i);
				ParcelRef parcel = new Parcel();
				parcel->state = Parcel::State_Alive;
				parcel->id = parcel_id;
				parcel->owner_id = UserID(0);
				parcel->admin_ids.push_back(UserID(0));
				parcel->writer_ids.push_back(UserID(0));
				parcel->created_time = TimeStamp::currentTime();
				parcel->zbounds = Vec2d(-2, 20);

				Vec2d centre(-105 + 210 * (i % 2), -105 + 210 * (i / 2));
				parcel->verts[0] = centre - Vec2d(-30, -30);
				parcel->verts[1] = centre - Vec2d(30, -30);
				parcel->verts[2] = centre - Vec2d(30, 30);
				parcel->verts[3] = centre - Vec2d(-30, 30);

				server.world_state->getRootWorldState()->parcels[parcel_id] = parcel;
			}
		}

		
		// Delete parcels newer than id 429.
		/*for(auto it = server.world_state->getRootWorldState()->parcels.begin(); it != server.world_state->getRootWorldState()->parcels.end();)
		{
			if(it->first.value() > 429)
				it = server.world_state->getRootWorldState()->parcels.erase(it);
			else
				it++;
		}*/
		

		// Recompute max_parcel_id
		max_parcel_id = ParcelID(0);
		for(auto it = server.world_state->getRootWorldState()->parcels.begin(); it != server.world_state->getRootWorldState()->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();
			max_parcel_id = myMax(max_parcel_id, parcel->id);
		}


		if(max_parcel_id.value() == 429)
		{
			// Make market and random east district
			const int start_id = (int)max_parcel_id.value() + 1;
			int next_id = start_id;

			// Make market district
			{
				PCG32 rng(1);

#ifdef WIN32
				Map2DRef road_map = PNGDecoder::decode("D:\\art\\substrata\\parcels\\roads.png");
#else
				Map2DRef road_map = PNGDecoder::decode("/var/www/cyberspace/roads.png");
#endif

				for(int i=0; i<300; ++i)
					makeRandomParcel(/*region botleft=*/Vec2d(335.f, 75), /*region topright=*/Vec2d(335.f + 130.f, 205.f), rng, next_id, server.world_state->getRootWorldState(), road_map,
						/*base width=*/3, /*rng width=*/4, /*base_h=*/4, /*rng_h=*/4);

				conPrint("Made market district, parcel ids " + toString(start_id) + " to " + toString(next_id - 1));
			}

			// Make random east district
			{
				const int east_district_start_id = next_id;

				PCG32 rng(1);

				for(int x = 0; x<4; ++x)
				for(int y = 0; y<4; ++y)
				{
					Vec2d offset(x * 70, y * 70);
					for(int i=0; i<100; ++i)
					{
						const Vec2d botleft = Vec2d(335.f, -275) + offset;
						makeRandomParcel(/*region botleft=*/botleft, /*region topright=*/botleft + Vec2d(60, 60), rng, next_id, server.world_state->getRootWorldState(), NULL/*road_map*/,
									/*base width=*/8, /*rng width=*/40, /*base_h=*/8, /*rng_h=*/20);
					}
				}

				conPrint("Made random east district, parcel ids " + toString(east_district_start_id) + " to " + toString(next_id - 1));
			}
		}


		// Add road objects
		if(false)
		{
			bool have_added_roads = false;
			for(auto it = server.world_state->getRootWorldState()->objects.begin(); it != server.world_state->getRootWorldState()->objects.end(); ++it)
			{
				const WorldObject* object = it->second.ptr();
				if(object->creator_id.value() == 0 && object->content == "road")
					have_added_roads = true;
			}

			printVar(have_added_roads);


			if(false)
			{
				// Remove all existing road objects (UID > 1000000)
				for(auto it = server.world_state->getRootWorldState()->objects.begin(); it != server.world_state->getRootWorldState()->objects.end();)
				{
					if(it->second->uid.value() >= 1000000)
						it = server.world_state->getRootWorldState()->objects.erase(it);
					else
						++it;
				}
			}

			if(!have_added_roads)
			{
				const UID next_uid = server.world_state->getNextObjectUID();
				conPrint(next_uid.toString());

				const float z_scale = 0.1;

				// Long roads near centre
				for(int x=-1; x <= 1; ++x)
				{
					if(x != 0)
					{
						makeRoad(*server.world_state,
							Vec3d(x * 92.5, 0, 0), // pos
							Vec3f(87, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

				for(int y=-1; y <= 1; ++y)
				{
					if(y != 0)
					{
						makeRoad(*server.world_state,
							Vec3d(0, y * 92.5, 0), // pos
							Vec3f(8, 87, z_scale), // scale
							0 // rot angle
						);
					}
				}

				// Diagonal roads
				{
					const float diag_z_scale = z_scale / 2; // to avoid z-fighting
					makeRoad(*server.world_state,
						Vec3d(57.5, 57.5, 0), // pos
						Vec3f(30, 6, diag_z_scale), // scale
						Maths::pi<float>() / 4 // rot angle
					);

					makeRoad(*server.world_state,
						Vec3d(57.5, -57.5, 0), // pos
						Vec3f(30, 6, diag_z_scale), // scale
						-Maths::pi<float>() / 4 // rot angle
					);

					makeRoad(*server.world_state,
						Vec3d(-57.5, 57.5, 0), // pos
						Vec3f(30, 6, diag_z_scale), // scale
						Maths::pi<float>() * 3 / 4 // rot angle
					);

					makeRoad(*server.world_state,
						Vec3d(-57.5, -57.5, 0), // pos
						Vec3f(30, 6, diag_z_scale), // scale
						-Maths::pi<float>() * 3 / 4 // rot angle
					);
				}

			
				// Roads along x axis:
				for(int x = -4; x <= 3; ++x)
				for(int y = -3; y <= 3; ++y)
				{
					bool near_centre = y >= -1 && y <= 1 && x >= -1 && x <= 0;

					bool long_roads = (x >= -2 && x <= 1) && y == 0;

					if(!near_centre && !long_roads)
					{
						makeRoad(*server.world_state,
							Vec3d(35 + x * 70, y * 70.0, 0), // pos
							Vec3f(62, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

				// Roads along y axis:
				for(int y = -4; y <= 3; ++y)
					for(int x = -3; x <= 3; ++x)
					{
						bool near_centre = x >= -1 && x <= 1 && y >= -1 && y <= 0;

						bool long_roads = (y >= -2 && y <= 1) && x == 0;

						if(!near_centre && !long_roads)
						{
							makeRoad(*server.world_state,
								Vec3d(x * 70.0, 35 + y * 70, 0), // pos
								Vec3f(8, 62, z_scale), // scale
								0 // rot angle
							);
						}
					}

				// Intersections
				for(int y = -3; y <= 3; ++y)
					for(int x = -3; x <= 3; ++x)
					{
						bool near_centre = x >= -1 && x <= 1 && y >= -1 && y <= 1;

						if(!near_centre)
						{
							makeRoad(*server.world_state,
								Vec3d(x * 70.0, y * 70, 0), // pos
								Vec3f(8, 8, z_scale), // scale
								0 // rot angle
							);
						}
					}

				// Intersections with diagonal roads (outer)
				for(int y = -1; y <= 1; ++y)
					for(int x = -1; x <= 1; ++x)
					{
						if(x != 0 && y != 0)
						{
							makeRoad(*server.world_state,
								Vec3d(x * 70.0, y * 70, 0), // pos
								Vec3f(8, 8, z_scale), // scale
								0 // rot angle
							);
						}
					}

				// Intersections with diagonal roads (inner)
				for(int y = -1; y <= 1; ++y)
					for(int x = -1; x <= 1; ++x)
					{
						if(x != 0 && y != 0)
						{
							makeRoad(*server.world_state,
								Vec3d(x * 45, y * 45, 0), // pos
								Vec3f(8, 8, z_scale), // scale
								0 // rot angle
							);
						}
					}

				// Centre roads
				makeRoad(*server.world_state,
					Vec3d(0, 45, 0), // pos
					Vec3f(82, 8, z_scale), // scale
					0 // rot angle
				);
				makeRoad(*server.world_state,
					Vec3d(0, -45, 0), // pos
					Vec3f(82, 8, z_scale), // scale
					0 // rot angle
				);
				makeRoad(*server.world_state,
					Vec3d(45, 0, 0), // pos
					Vec3f(8, 82, z_scale), // scale
					0 // rot angle
				);
				makeRoad(*server.world_state,
					Vec3d(-45, 0, 0), // pos
					Vec3f(8, 82, z_scale), // scale
					0 // rot angle
				);
			}
		}




		server.world_state->denormaliseData();



		//-------------------------------- Launch webserver ---------------------------------------------------------

		// Create TLS configuration
		struct tls_config* web_tls_configuration = tls_config_new();

#ifdef WIN32
		if(tls_config_set_cert_file(web_tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()/*"O:\\new_cyberspace\\trunk\\scripts\\cert.pem"*/) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));
		if(tls_config_set_key_file(web_tls_configuration, (server_state_dir + "/MyKey.key").c_str() /*"O:\\new_cyberspace\\trunk\\scripts\\key.pem"*/) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#else
		if(tls_config_set_cert_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");
		if(tls_config_set_key_file(web_tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#endif

		Reference<WebDataStore> web_data_store = new WebDataStore();

#ifdef WIN32		
		web_data_store->public_files_dir = "N:\\new_cyberspace\\trunk\\webserver_public_files";
		//web_data_store->public_files_dir = "C:\\programming\\cyberspace\\webdata\\public_files";
		//web_data_store->resources_dir    = "C:\\programming\\new_cyberspace\\webdata\\resources";
		web_data_store->letsencrypt_webroot = "C:\\programming\\cyberspace\\webdata\\letsencrypt_webroot";
#else
		web_data_store->public_files_dir = "/var/www/cyberspace/public_html";
		//web_data_store->resources_dir    = "/var/www/cyberspace/resources";
		web_data_store->letsencrypt_webroot = "/var/www/cyberspace/letsencrypt_webroot";
#endif
		conPrint("webserver public_files_dir: " + web_data_store->public_files_dir);

		Reference<WebServerSharedRequestHandler> shared_request_handler = new WebServerSharedRequestHandler();
		shared_request_handler->data_store = web_data_store.ptr();
		shared_request_handler->world_state = server.world_state.ptr();

		ThreadManager web_thread_manager;
		web_thread_manager.addThread(new web::WebListenerThread(80,  shared_request_handler.getPointer(), NULL));
		web_thread_manager.addThread(new web::WebListenerThread(443, shared_request_handler.getPointer(), web_tls_configuration));

		// While Coinbase webhooks are not working, add a Coinbase polling thread.
		web_thread_manager.addThread(new CoinbasePollerThread(server.world_state.ptr()));

		web_thread_manager.addThread(new OpenSeaPollerThread(server.world_state.ptr()));


		//-----------------------------------------------------------------------------------------


		// Create TLS configuration for substrata protocol server
		struct tls_config* tls_configuration = tls_config_new();

#ifdef WIN32
		// NOTE: key generated with 
		// cd D:\programming\LibreSSL\libressl-2.8.3-x64-vs2019-install\bin
		// ./openssl req -new -newkey rsa:4096 -x509 -sha256 -days 3650 -nodes -out MyCertificate.crt -keyout MyKey.key
		if(tls_config_set_cert_file(tls_configuration, (server_state_dir + "/MyCertificate.crt").c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");
		
		if(tls_config_set_key_file(tls_configuration, (server_state_dir + "/MyKey.key").c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#else
		// For now just use our web Let's Encrypt cert and private key.
		if(tls_config_set_cert_file(tls_configuration, "/etc/letsencrypt/live/substrata.info/cert.pem") != 0)
			throw glare::Exception("tls_config_set_cert_file failed.");

		if(tls_config_set_key_file(tls_configuration, "/etc/letsencrypt/live/substrata.info/privkey.pem") != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed.");
#endif

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server, tls_configuration));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		thread_manager.addThread(new MeshLODGenThread(server.world_state.ptr()));

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
								writeToNetworkStream(*avatar, packet);

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
								packet.writeUInt32(avatar->anim_state);

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
							// Send ObjectLightmapURLChanged packet
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ObjectLightmapURLChanged);
							writeToStream(ob->uid, packet);
							packet.writeStringLengthFirst(ob->lightmap_url);

							enqueuePacketToBroadcast(packet, world_packets);

							ob->from_remote_lightmap_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_model_url_dirty)
						{
							// Send ObjectModelURLChanged packet
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ObjectModelURLChanged);
							writeToStream(ob->uid, packet);
							packet.writeStringLengthFirst(ob->model_url);

							enqueuePacketToBroadcast(packet, world_packets);

							ob->from_remote_model_url_dirty = false;
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

				Lock lock3(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					assert(dynamic_cast<WorkerThread*>(i->getPointer()));
					static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet);
				}
			}


			if((loop_iter % 128) == 0) // Approx every 10 s.
			{
				// Want want to list new parcels (to bring the total number being listed up to our target number) every day at midnight UTC.
				int hour, day, year;
				Clock::getHourDayOfYearAndYear(Clock::getSecsSince1970(), hour, day, year);

				const bool different_day = 
					server.world_state->last_parcel_sale_update_year != year ||
					server.world_state->last_parcel_sale_update_day != day;

				const bool initial_listing = server.world_state->last_parcel_sale_update_year == 0;

				/* TEMP NO AUTO PARCEL LISTINGS
				if(initial_listing || different_day)
				{
					updateParcelSales(*server.world_state);
					server.world_state->last_parcel_sale_update_hour = hour;
					server.world_state->last_parcel_sale_update_day = day;
					server.world_state->last_parcel_sale_update_year = year;
				}*/
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
