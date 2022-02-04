/*=====================================================================
ServerWorldState.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#include "ServerWorldState.h"


#include <FileInStream.h>
#include <FileOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Lock.h>
#include <Clock.h>
#include <Timer.h>
#include <Database.h>
#include <BufferOutStream.h>
#include <BufferInStream.h>


ServerAllWorldsState::ServerAllWorldsState()
{
	next_avatar_uid = UID(0);
	next_object_uid = UID(0);
	next_order_uid = 0;
	next_sub_eth_transaction_uid = 0;

	world_states[""] = new ServerWorldState();

	last_parcel_update_info.last_parcel_sale_update_hour = 0;
	last_parcel_update_info.last_parcel_sale_update_day = 0;
	last_parcel_update_info.last_parcel_sale_update_year = 0;

	BTC_per_EUR = 0;
	ETH_per_EUR = 0;

	eth_info.min_next_nonce = 0;

	server_admin_message_changed = false;

	read_only_mode = false;
}


ServerAllWorldsState::~ServerAllWorldsState()
{
}


void ServerAllWorldsState::createNewDatabase(const std::string& path)
{
	conPrint("Creating new world state database at '" + path + "'...");

	database.openAndMakeOrClearDatabase(path);
}


static const uint32 WORLD_STATE_MAGIC_NUMBER = 487173571;
static const uint32 WORLD_STATE_SERIALISATION_VERSION = 3; // v3: using Database
static const uint32 WORLD_CHUNK = 50;
static const uint32 WORLD_OBJECT_CHUNK = 100;
static const uint32 USER_CHUNK = 101;
static const uint32 PARCEL_CHUNK = 102;
static const uint32 RESOURCE_CHUNK = 103;
static const uint32 ORDER_CHUNK = 104;
static const uint32 USER_WEB_SESSION_CHUNK = 105;
static const uint32 PARCEL_AUCTION_CHUNK = 106;
static const uint32 SCREENSHOT_CHUNK = 107;
static const uint32 SUB_ETH_TRANSACTIONS_CHUNK = 108;
static const uint32 LAST_PARCEL_SALE_UPDATE_CHUNK = 109;
static const uint32 MAP_TILE_INFO_CHUNK = 110;
static const uint32 ETH_INFO_CHUNK = 111;
static const uint32 EOS_CHUNK = 1000;


static const uint32 PARCEL_SALE_UPDATE_VERSION = 1;
static const uint32 MAP_TILE_INFO_VERSION = 1;
static const uint32 ETH_INFO_CHUNK_VERSION = 1;


void ServerAllWorldsState::readFromDisk(const std::string& path)
{
	conPrint("Reading world state from '" + path + "'...");
	Timer timer;

	size_t num_obs = 0;
	size_t num_parcels = 0;
	size_t num_orders = 0;
	size_t num_sessions = 0;
	size_t num_auctions = 0;
	size_t num_screenshots = 0;
	size_t num_sub_eth_transactions = 0;
	size_t num_tiles_read = 0;

	bool is_pre_database_format = false;
	{
		FileInStream stream(path);

		// Read magic number
		const uint32 m = stream.readUInt32();
		is_pre_database_format = m == WORLD_STATE_MAGIC_NUMBER;
	}

	if(!is_pre_database_format)
	{
		// Using database
		database.startReadingFromDisk(path);

		BufferInStream stream;
		for(auto it = database.getRecordMap().begin(); it != database.getRecordMap().end(); ++it)
		{
			const DatabaseKey database_key = it->first;
			const Database::RecordInfo& record = it->second;

			if(record.isRecordValid())
			{
				stream.clear();
				stream.buf.resizeNoCopy(record.len);
				if(record.len > 0)
					std::memcpy(stream.buf.data(), database.getInitialRecordData(record), record.len); // Copy from DB to our temp buffer

				// Now deserialise from our temp buffer
				const uint32 chunk = stream.readUInt32();
				if(chunk == WORLD_CHUNK)
				{
					// Not doing anything wtih this chunk.  Instead the world name is saved with each object and parcel.
				}
				else if(chunk == WORLD_OBJECT_CHUNK)
				{
					// Read world name
					const std::string world_name = stream.readStringLengthFirst(10000);

					// Create ServerWorldState for world name if needed
					if(world_states.count(world_name) == 0) 
						world_states[world_name] = new ServerWorldState();

					// Deserialise object
					WorldObjectRef world_ob = new WorldObject();
					readFromStream(stream, *world_ob);

					//TEMP HACK: clear lightmap needed flag
					BitUtils::zeroBit(world_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);

					world_ob->database_key = database_key;
					world_states[world_name]->objects[world_ob->uid] = world_ob; // Add to object map
					num_obs++;

					next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
				}
				else if(chunk == USER_CHUNK)
				{
					// Deserialise user
					UserRef user = new User();
					readFromStream(stream, *user);

					user->database_key = database_key;
					user_id_to_users[user->id] = user; // Add to user map
					name_to_users[user->name] = user; // Add to user map
				}
				else if(chunk == PARCEL_CHUNK)
				{
					// Read world name
					const std::string world_name = stream.readStringLengthFirst(10000);

					// Create ServerWorldState for world name if needed
					if(world_states.count(world_name) == 0) 
						world_states[world_name] = new ServerWorldState();

					// Deserialise parcel
					ParcelRef parcel = new Parcel();
					readFromStream(stream, *parcel);

					parcel->database_key = database_key;
					world_states[world_name]->parcels[parcel->id] = parcel; // Add to parcel map
					num_parcels++;
				}
				else if(chunk == RESOURCE_CHUNK)
				{
					// Deserialise resource
					ResourceRef resource = new Resource();
					readFromStream(stream, *resource);

					//conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

					// TEMP HACK: Rewrite resource local path for testing server state on dev machine.
					/*if(!resource->URL.empty())
					{
					resource->setLocalPath(this->resource_manager->computeDefaultLocalPathForURL(resource->URL));
					if(FileUtils::fileExists(resource->getLocalPath()))
					resource->setState(Resource::State_Present);
					}*/

					resource->database_key = database_key;
					this->resource_manager->addResource(resource);
				}
				else if(chunk == ORDER_CHUNK)
				{
					// Deserialise order
					OrderRef order = new Order();
					readFromStream(stream, *order);

					order->database_key = database_key;
					orders[order->id] = order; // Add to order map

					next_order_uid = myMax(order->id + 1, next_order_uid);
					num_orders++;
				}
				else if(chunk == USER_WEB_SESSION_CHUNK)
				{
					// Deserialise UserWebSession
					UserWebSessionRef session = new UserWebSession();
					readFromStream(stream, *session);

					session->database_key = database_key;
					user_web_sessions[session->id] = session; // Add to session map
					num_sessions++;
				}
				else if(chunk == PARCEL_AUCTION_CHUNK)
				{
					// Deserialise ParcelAuction
					ParcelAuctionRef auction = new ParcelAuction();
					readFromStream(stream, *auction);

					auction->database_key = database_key;
					parcel_auctions[auction->id] = auction;
					num_auctions++;
				}
				else if(chunk == SCREENSHOT_CHUNK)
				{
					// Deserialise Screenshot
					ScreenshotRef shot = new Screenshot();
					readFromStream(stream, *shot);

					shot->database_key = database_key;
					screenshots[shot->id] = shot;
					num_screenshots++;
				}
				else if(chunk == SUB_ETH_TRANSACTIONS_CHUNK)
				{
					// Deserialise Screenshot
					SubEthTransactionRef trans = new SubEthTransaction();
					readFromStream(stream, *trans);

					next_sub_eth_transaction_uid = myMax(trans->id + 1, next_sub_eth_transaction_uid);

					trans->database_key = database_key;
					sub_eth_transactions[trans->id] = trans;
					num_sub_eth_transactions++;
				}
				else if(chunk == ETH_INFO_CHUNK)
				{
					const uint32 eth_info_v = stream.readInt32();
					if(eth_info_v != ETH_INFO_CHUNK_VERSION)
						throw glare::Exception("invalid eth_info version: " + toString(eth_info_v));

					this->eth_info.database_key = database_key;
					this->eth_info.min_next_nonce = stream.readInt32();
				}
				else if(chunk == LAST_PARCEL_SALE_UPDATE_CHUNK)
				{
					const uint32 update_v = stream.readInt32();
					if(update_v != PARCEL_SALE_UPDATE_VERSION)
						throw glare::Exception("invalid parcel_sale_update_version: " + toString(update_v));

					this->last_parcel_update_info.database_key = database_key;
					this->last_parcel_update_info.last_parcel_sale_update_hour = stream.readInt32();
					this->last_parcel_update_info.last_parcel_sale_update_day = stream.readInt32();
					this->last_parcel_update_info.last_parcel_sale_update_year = stream.readInt32();
				}
				else if(chunk == MAP_TILE_INFO_CHUNK)
				{
					const uint32 map_tile_info_version = stream.readInt32();
					if(map_tile_info_version != MAP_TILE_INFO_VERSION)
						throw glare::Exception("invalid map_tile_info_version: " + toString(map_tile_info_version));

					const int num_tiles = stream.readInt32();
					for(int i=0; i<num_tiles; ++i)
					{
						const int x = stream.readInt32();
						const int y = stream.readInt32();
						const int z = stream.readInt32();

						TileInfo tile_info;
						const bool cur_tile_screenshot_non_null = stream.readInt32() != 0;
						if(cur_tile_screenshot_non_null)
						{
							tile_info.cur_tile_screenshot = new Screenshot();
							readFromStream(stream, *tile_info.cur_tile_screenshot);
						}
						const bool prev_tile_screenshot_non_null = stream.readInt32() != 0;
						if(prev_tile_screenshot_non_null)
						{
							tile_info.prev_tile_screenshot = new Screenshot();
							readFromStream(stream, *tile_info.prev_tile_screenshot);
						}

						map_tile_info.info[Vec3<int>(x, y, z)] = tile_info; // Insert
					}

					map_tile_info.database_key = database_key;

					num_tiles_read = num_tiles;
				}
				else if(chunk == EOS_CHUNK)
				{
					break;
				}
				else
				{
					throw glare::Exception("Unknown chunk type '" + toString(chunk) + "'");
				}
			}
		}


		database.finishReadingFromDisk();
	}
	else // Else if is_pre_database:
	{
		Reference<ServerWorldState> current_world = new ServerWorldState();
		world_states[""] = current_world;

		FileInStream stream(path);

		// Read magic number
		const uint32 m = stream.readUInt32();
		if(m != WORLD_STATE_MAGIC_NUMBER)
			throw glare::Exception("Invalid magic number " + toString(m) + ", expected " + toString(WORLD_STATE_MAGIC_NUMBER) + ".");

		// Read version
		const uint32 v = stream.readUInt32();
		if(v > WORLD_STATE_SERIALISATION_VERSION)
			throw glare::Exception("Unknown version " + toString(v) + ", expected " + toString(WORLD_STATE_SERIALISATION_VERSION) + ".");

		while(1)
		{
			const uint32 chunk = stream.readUInt32();
			if(chunk == WORLD_CHUNK)
			{
				const std::string world_name = stream.readStringLengthFirst(1000);
				if(world_states.count(world_name) == 0)
					world_states[world_name] = new ServerWorldState();

				current_world = world_states[world_name];
			}
			else if(chunk == WORLD_OBJECT_CHUNK)
			{
				// Deserialise object
				WorldObjectRef world_ob = new WorldObject();
				readFromStream(stream, *world_ob);

				//TEMP HACK: clear lightmap needed flag
				BitUtils::zeroBit(world_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);

				current_world->objects[world_ob->uid] = world_ob; // Add to object map
				num_obs++;

				next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
			}
			else if(chunk == USER_CHUNK)
			{
				// Deserialise user
				UserRef user = new User();
				readFromStream(stream, *user);

				user_id_to_users[user->id] = user; // Add to user map
				name_to_users[user->name] = user; // Add to user map
			}
			else if(chunk == PARCEL_CHUNK)
			{
				// Deserialise parcel
				ParcelRef parcel = new Parcel();
				readFromStream(stream, *parcel);

				current_world->parcels[parcel->id] = parcel; // Add to parcel map
				num_parcels++;
			}
			else if(chunk == RESOURCE_CHUNK)
			{
				// Deserialise resource
				ResourceRef resource = new Resource();
				readFromStream(stream, *resource);

				//conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

				// TEMP HACK: Rewrite resource local path for testing server state on dev machine.
				/*if(!resource->URL.empty())
				{
					resource->setLocalPath(this->resource_manager->computeDefaultLocalPathForURL(resource->URL));
					if(FileUtils::fileExists(resource->getLocalPath()))
						resource->setState(Resource::State_Present);
				}*/

				this->resource_manager->addResource(resource);
			}
			else if(chunk == ORDER_CHUNK)
			{
				// Deserialise order
				OrderRef order = new Order();
				readFromStream(stream, *order);

				orders[order->id] = order; // Add to order map

				next_order_uid = myMax(order->id + 1, next_order_uid);
				num_orders++;
			}
			else if(chunk == USER_WEB_SESSION_CHUNK)
			{
				// Deserialise UserWebSession
				UserWebSessionRef session = new UserWebSession();
				readFromStream(stream, *session);

				user_web_sessions[session->id] = session; // Add to session map
				num_sessions++;
			}
			else if(chunk == PARCEL_AUCTION_CHUNK)
			{
				// Deserialise ParcelAuction
				ParcelAuctionRef auction = new ParcelAuction();
				readFromStream(stream, *auction);

				parcel_auctions[auction->id] = auction;
				num_auctions++;
			}
			else if(chunk == SCREENSHOT_CHUNK)
			{
				// Deserialise Screenshot
				ScreenshotRef shot = new Screenshot();
				readFromStream(stream, *shot);

				screenshots[shot->id] = shot;
				num_screenshots++;
			}
			else if(chunk == SUB_ETH_TRANSACTIONS_CHUNK)
			{
				// Deserialise Screenshot
				SubEthTransactionRef trans = new SubEthTransaction();
				readFromStream(stream, *trans);

				next_sub_eth_transaction_uid = myMax(trans->id + 1, next_sub_eth_transaction_uid);

				sub_eth_transactions[trans->id] = trans;
				num_sub_eth_transactions++;
			}
			else if(chunk == ETH_INFO_CHUNK)
			{
				const uint32 eth_info_v = stream.readInt32();
				if(eth_info_v != ETH_INFO_CHUNK_VERSION)
					throw glare::Exception("invalid eth_info version: " + toString(eth_info_v));

				this->eth_info.min_next_nonce = stream.readInt32();
			}
			else if(chunk == LAST_PARCEL_SALE_UPDATE_CHUNK)
			{
				const uint32 update_v = stream.readInt32();
				if(update_v != PARCEL_SALE_UPDATE_VERSION)
					throw glare::Exception("invalid parcel_sale_update_version: " + toString(update_v));
				this->last_parcel_update_info.last_parcel_sale_update_hour = stream.readInt32();
				this->last_parcel_update_info.last_parcel_sale_update_day = stream.readInt32();
				this->last_parcel_update_info.last_parcel_sale_update_year = stream.readInt32();
			}
			else if(chunk == MAP_TILE_INFO_CHUNK)
			{
				const uint32 map_tile_info_version = stream.readInt32();
				if(map_tile_info_version != MAP_TILE_INFO_VERSION)
					throw glare::Exception("invalid map_tile_info_version: " + toString(map_tile_info_version));

				const int num_tiles = stream.readInt32();
				for(int i=0; i<num_tiles; ++i)
				{
					const int x = stream.readInt32();
					const int y = stream.readInt32();
					const int z = stream.readInt32();
				
					TileInfo tile_info;
					const bool cur_tile_screenshot_non_null = stream.readInt32() != 0;
					if(cur_tile_screenshot_non_null)
					{
						tile_info.cur_tile_screenshot = new Screenshot();
						readFromStream(stream, *tile_info.cur_tile_screenshot);
					}
					const bool prev_tile_screenshot_non_null = stream.readInt32() != 0;
					if(prev_tile_screenshot_non_null)
					{
						tile_info.prev_tile_screenshot = new Screenshot();
						readFromStream(stream, *tile_info.prev_tile_screenshot);
					}

					map_tile_info.info[Vec3<int>(x, y, z)] = tile_info; // Insert
				}
				num_tiles_read = num_tiles;
			}
			else if(chunk == EOS_CHUNK)
			{
				break;
			}
			else
			{
				throw glare::Exception("Unknown chunk type '" + toString(chunk) + "'");
			}
		}
	}


	// If we were loading the old pre-database format:
	if(is_pre_database_format)
	{
		database.openAndMakeOrClearDatabase(path);

		// Add everything to dirty sets so it gets saved to the DB initially.
		addEverythingToDirtySets();
	}


	denormaliseData();

	// Compress voxel data if needed.
	for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	{
		Reference<ServerWorldState> world_state = world_it->second;
		for(auto it = world_state->objects.begin(); it != world_state->objects.end(); ++it)
		{
			/*WorldObject* ob = it->second.ptr();
			if(!ob->voxel_group.voxels.empty() && ob->compressed_voxels.empty())
			{
				WorldObject::compressVoxelGroup(ob->voxel_group, ob->compressed_voxels);
			}*/
			//ob->compressVoxels();
		}
	}

	//TEMP: create screenshots for parcels if not already done.
#if 0
	{
		//TEMP: scan over all screenshots and find highest used ID. 
		uint64 highest_shot_id = 0;
		for(auto it = screenshots.begin(); it != screenshots.end(); ++it)
			highest_shot_id = myMax(highest_shot_id, it->first);

		uint64 next_shot_id = highest_shot_id + 1;
		for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
		{
			Reference<ServerWorldState> world_state = world_it->second;
			for(auto it = world_state->parcels.begin(); it != world_state->parcels.end(); ++it)
			{
				Parcel* parcel = it->second.ptr();

				if(parcel->screenshot_ids.size() < 2)
				{
					// Create close-in screenshot
					{
						ScreenshotRef shot = new Screenshot();
						shot->id = next_shot_id++;
						parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
						shot->width_px = 650;
						shot->highlight_parcel_id = (int)parcel->id.value();
						shot->created_time = TimeStamp::currentTime();
						shot->state = Screenshot::ScreenshotState_notdone;

						screenshots[shot->id] = shot;

						parcel->screenshot_ids.push_back(shot->id);
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

						screenshots[shot->id] = shot;

						parcel->screenshot_ids.push_back(shot->id);
					}
				}
			}
		}
	}
#endif


	conPrint("min_next_nonce: " + toString(eth_info.min_next_nonce));
	conPrint("Loaded " + toString(num_obs) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " +
		toString(num_parcels) + " parcel(s), " + toString(resource_manager->getResourcesForURL().size()) + " resource(s), " + toString(num_orders) + " order(s), " + 
		toString(num_sessions) + " session(s), " + toString(num_auctions) + " auction(s), " + toString(num_screenshots) + " screenshot(s), " + 
		toString(num_sub_eth_transactions) + " sub eth transaction(s), " + toString(num_tiles_read) + " tiles in " + timer.elapsedStringNSigFigs(4));
}


void ServerAllWorldsState::addEverythingToDirtySets()
{
	for(auto it = resource_manager->getResourcesForURL().begin(); it != resource_manager->getResourcesForURL().end(); ++it)
		db_dirty_resources.insert(it->second);

	for(auto it = user_id_to_users.begin(); it != user_id_to_users.end(); ++it)
		db_dirty_users.insert(it->second);

	for(auto it = orders.begin(); it != orders.end(); ++it)
		db_dirty_orders.insert(it->second);

	for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	{
		Reference<ServerWorldState> world_state = world_it->second;

		for(auto it = world_state->objects.begin(); it != world_state->objects.end(); ++it)
			world_state->db_dirty_world_objects.insert(it->second);

		for(auto it = world_state->parcels.begin(); it != world_state->parcels.end(); ++it)
			world_state->db_dirty_parcels.insert(it->second);
	}

	for(auto it = user_web_sessions.begin(); it != user_web_sessions.end(); ++it)
		db_dirty_userwebsessions.insert(it->second);

	for(auto it = parcel_auctions.begin(); it != parcel_auctions.end(); ++it)
		db_dirty_parcel_auctions.insert(it->second);

	for(auto it = screenshots.begin(); it != screenshots.end(); ++it)
		db_dirty_screenshots.insert(it->second);

	for(auto it = sub_eth_transactions.begin(); it != sub_eth_transactions.end(); ++it)
		db_dirty_sub_eth_transactions.insert(it->second);

	map_tile_info.db_dirty = true;

	last_parcel_update_info.db_dirty = true;

	eth_info.db_dirty = true;
}


void ServerAllWorldsState::denormaliseData()
{
	for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	{
		Reference<ServerWorldState> world_state = world_it->second;

		// Build cached fields like WorldObject::creator_name
		for(auto i=world_state->objects.begin(); i != world_state->objects.end(); ++i)
		{
			auto res = user_id_to_users.find(i->second->creator_id);
			if(res != user_id_to_users.end())
				i->second->creator_name = res->second->name;
		}

		for(auto i=world_state->parcels.begin(); i != world_state->parcels.end(); ++i)
		{
			Parcel* parcel = i->second.ptr();

			// Denormalise Parcel::owner_name
			{
				auto res = user_id_to_users.find(parcel->owner_id); // Lookup user from owner_id
				if(res != user_id_to_users.end())
					parcel->owner_name = res->second->name;
			}

			// Denormalise Parcel::admin_names
			parcel->admin_names.resize(parcel->admin_ids.size());
			for(size_t z=0; z<parcel->admin_ids.size(); ++z)
			{
				auto res = user_id_to_users.find(parcel->admin_ids[z]); // Lookup user from admin id
				if(res != user_id_to_users.end())
				{
					//conPrint("admin: " + res->second->name);
					parcel->admin_names[z] = res->second->name;
				}
			}

			// Denormalise Parcel::writer_names
			parcel->writer_names.resize(parcel->writer_ids.size());
			for(size_t z=0; z<parcel->writer_ids.size(); ++z)
			{
				auto res = user_id_to_users.find(parcel->writer_ids[z]); // Lookup user from writer id
				if(res != user_id_to_users.end())
				{
					//conPrint("writer: " + res->second->name);
					parcel->writer_names[z] = res->second->name;
				}
			}
		}
	}
}


// Write any changed data (objects in dirty set) to disk.
void ServerAllWorldsState::serialiseToDisk(const std::string& path)
{
	conPrint("Saving world state to disk...");
	Timer timer;

	try
	{
		// Number of various type of objects that were dirty and saved.
		size_t num_obs = 0;
		size_t num_parcels = 0;
		size_t num_orders = 0;
		size_t num_sessions = 0;
		size_t num_auctions = 0;
		size_t num_screenshots = 0;
		size_t num_sub_eth_transactions = 0;
		size_t num_tiles_written = 0;
		size_t num_users = 0;
		size_t num_resources = 0;

		// First, delete any records in db_records_to_delete.  (This has the keys of deleted objects etc..)
		for(auto it = db_records_to_delete.begin(); it != db_records_to_delete.end(); ++it)
		{
			const DatabaseKey key = *it;
			database.deleteRecord(key);
		}
		db_records_to_delete.clear();

		
		BufferOutStream temp_buf;

		// Iterate over all objects, if they are dirty, write to the DB

		// For each world
		for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
		{
			const std::string world_name = world_it->first;
			Reference<ServerWorldState> world_state = world_it->second;

			// Write objects
			{
				for(auto it = world_state->db_dirty_world_objects.begin(); it != world_state->db_dirty_world_objects.end(); ++it)
				{
					WorldObject* ob = it->ptr();
					temp_buf.clear();
					temp_buf.writeUInt32(WORLD_OBJECT_CHUNK);
					temp_buf.writeStringLengthFirst(world_name); // Write world name
					ob->writeToStream(temp_buf); // Write object

					if(!ob->database_key.valid())
						ob->database_key = database.allocUnusedKey(); // Get a new key

					database.updateRecord(ob->database_key, temp_buf.buf);

					num_obs++;
				}

				world_state->db_dirty_world_objects.clear();
			}

			// Write parcels
			{
				for(auto it = world_state->db_dirty_parcels.begin(); it != world_state->db_dirty_parcels.end(); ++it)
				{
					Parcel* parcel = it->ptr();
					temp_buf.clear();
					temp_buf.writeUInt32(PARCEL_CHUNK);
					temp_buf.writeStringLengthFirst(world_name); // Write world name
					writeToStream(*parcel, temp_buf); // Write parcel

					if(!parcel->database_key.valid())
						parcel->database_key = database.allocUnusedKey(); // Get a new key

					database.updateRecord(parcel->database_key, temp_buf.buf);

					num_parcels++;
				}

				world_state->db_dirty_parcels.clear();
			}
		}

		// Write users
		{
			for(auto it=db_dirty_users.begin(); it != db_dirty_users.end(); ++it)
			{
				User* user = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(USER_CHUNK);
				writeToStream(*user, temp_buf);

				if(!user->database_key.valid())
					user->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(user->database_key, temp_buf.buf);

				num_users++;
			}

			db_dirty_users.clear();
		}

		// Write resource objects
		{
			for(auto i=db_dirty_resources.begin(); i != db_dirty_resources.end(); ++i)
			{
				Resource* resource = i->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(RESOURCE_CHUNK);
				writeToStream(*resource, temp_buf);

				if(!resource->database_key.valid())
					resource->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(resource->database_key, temp_buf.buf);

				num_resources++;
			}

			db_dirty_resources.clear();
		}

		// Write orders
		{
			for(auto i=db_dirty_orders.begin(); i != db_dirty_orders.end(); ++i)
			{
				Order* order = i->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(ORDER_CHUNK);
				writeToStream(*order, temp_buf);

				if(!order->database_key.valid())
					order->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(order->database_key, temp_buf.buf);

				num_orders++;
			}

			db_dirty_orders.clear();
		}

		// Write UserWebSessions
		{
			for(auto i=db_dirty_userwebsessions.begin(); i != db_dirty_userwebsessions.end(); ++i)
			{
				UserWebSession* session = i->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(USER_WEB_SESSION_CHUNK);
				writeToStream(*session, temp_buf);

				if(!session->database_key.valid())
					session->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(session->database_key, temp_buf.buf);

				num_sessions++;
			}

			db_dirty_userwebsessions.clear();
		}

		// Write ParcelAuctions
		{
			for(auto i=db_dirty_parcel_auctions.begin(); i != db_dirty_parcel_auctions.end(); ++i)
			{
				ParcelAuction* auction = i->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(PARCEL_AUCTION_CHUNK);
				writeToStream(*auction, temp_buf);

				if(!auction->database_key.valid())
					auction->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(auction->database_key, temp_buf.buf);

				num_auctions++;
			}

			db_dirty_parcel_auctions.clear();
		}

		// Write Screenshots
		{
			for(auto it=db_dirty_screenshots.begin(); it != db_dirty_screenshots.end(); ++it)
			{
				Screenshot* shot = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(SCREENSHOT_CHUNK);
				writeToStream(*shot, temp_buf);

				if(!shot->database_key.valid())
					shot->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(shot->database_key, temp_buf.buf);

				num_screenshots++;
			}

			db_dirty_screenshots.clear();
		}

		// Write SubEthTransactions
		{
			for(auto i=db_dirty_sub_eth_transactions.begin(); i != db_dirty_sub_eth_transactions.end(); ++i)
			{
				SubEthTransaction* trans = i->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(SUB_ETH_TRANSACTIONS_CHUNK);
				writeToStream(*trans, temp_buf);

				if(!trans->database_key.valid())
					trans->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(trans->database_key, temp_buf.buf);

				num_sub_eth_transactions++;
			}

			db_dirty_sub_eth_transactions.clear();
		}

		// Write MAP_TILE_INFO_CHUNK
		if(map_tile_info.db_dirty)
		{
			temp_buf.clear();
			temp_buf.writeUInt32(MAP_TILE_INFO_CHUNK);
			temp_buf.writeUInt32(MAP_TILE_INFO_VERSION);
			temp_buf.writeInt32((int)map_tile_info.info.size());
			for(auto it=map_tile_info.info.begin(); it != map_tile_info.info.end(); ++it)
			{
				Vec3<int> v = it->first;
				const TileInfo& tile_info = it->second;

				temp_buf.writeInt32(v.x);
				temp_buf.writeInt32(v.y);
				temp_buf.writeInt32(v.z);

				temp_buf.writeInt32(tile_info.cur_tile_screenshot.nonNull() ? 1 : 0);
				if(tile_info.cur_tile_screenshot.nonNull())
					writeToStream(*tile_info.cur_tile_screenshot, temp_buf);

				temp_buf.writeInt32(tile_info.prev_tile_screenshot.nonNull() ? 1 : 0);
				if(tile_info.prev_tile_screenshot.nonNull())
					writeToStream(*tile_info.prev_tile_screenshot, temp_buf);
			}

			if(!map_tile_info.database_key.valid())
				map_tile_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(map_tile_info.database_key, temp_buf.buf);

			map_tile_info.db_dirty = false;

			num_tiles_written = map_tile_info.info.size();
		}

		// Write LAST_PARCEL_SALE_UPDATE_CHUNK
		if(last_parcel_update_info.db_dirty)
		{
			temp_buf.clear();
			temp_buf.writeUInt32(LAST_PARCEL_SALE_UPDATE_CHUNK);
			temp_buf.writeUInt32(PARCEL_SALE_UPDATE_VERSION);
			temp_buf.writeInt32(this->last_parcel_update_info.last_parcel_sale_update_hour);
			temp_buf.writeInt32(this->last_parcel_update_info.last_parcel_sale_update_day);
			temp_buf.writeInt32(this->last_parcel_update_info.last_parcel_sale_update_year);

			if(!last_parcel_update_info.database_key.valid())
				last_parcel_update_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(last_parcel_update_info.database_key, temp_buf.buf);

			last_parcel_update_info.db_dirty = false;
		}

		// Write ETH_INFO_CHUNK
		if(eth_info.db_dirty)
		{
			temp_buf.clear();
			temp_buf.writeUInt32(ETH_INFO_CHUNK);
			temp_buf.writeUInt32(ETH_INFO_CHUNK_VERSION);
			temp_buf.writeInt32(this->eth_info.min_next_nonce);

			if(!eth_info.database_key.valid())
				eth_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(eth_info.database_key, temp_buf.buf);

			eth_info.db_dirty = false;
		}

		database.flush();

		conPrint("Saved " + toString(num_obs) + " object(s), " + toString(num_users) + " user(s), " +
			toString(num_parcels) + " parcel(s), " + toString(num_resources) + " resource(s), " + toString(num_orders) + " order(s), " + 
			toString(num_sessions) + " session(s), " + toString(num_auctions) + " auction(s), " + toString(num_screenshots) + " screenshot(s), " +
			toString(num_sub_eth_transactions) + " sub eth transction(s), " + toString(num_tiles_written) + " tiles in " + timer.elapsedStringNSigFigs(4));
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


UID ServerAllWorldsState::getNextObjectUID()
{
	const UID next = next_object_uid;
	next_object_uid = UID(next_object_uid.value() + 1);
	return next;
}


UID ServerAllWorldsState::getNextAvatarUID()
{
	Lock lock(mutex);

	const UID next = next_avatar_uid;
	next_avatar_uid = UID(next_avatar_uid.value() + 1);
	return next;
}


uint64 ServerAllWorldsState::getNextOrderUID()
{
	Lock lock(mutex);
	return next_order_uid++;
}


uint64 ServerAllWorldsState::getNextSubEthTransactionUID()
{
	Lock lock(mutex);
	return next_sub_eth_transaction_uid++;
}


uint64 ServerAllWorldsState::getNextScreenshotUID()
{
	Lock lock(mutex);

	uint64 highest_id = 0;

	for(auto it = screenshots.begin(); it != screenshots.end(); ++it)
		highest_id = myMax(highest_id, it->first);


	// Consider ids from map tile screenshots as well
	for(auto it = map_tile_info.info.begin(); it != map_tile_info.info.end(); ++it)
	{
		const TileInfo& tile_info = it->second;

		if(tile_info.cur_tile_screenshot.nonNull())
			highest_id = myMax(highest_id, tile_info.cur_tile_screenshot->id);

		if(tile_info.prev_tile_screenshot.nonNull())
			highest_id = myMax(highest_id, tile_info.prev_tile_screenshot->id);
	}

	return highest_id + 1;
}


void ServerAllWorldsState::setUserWebMessage(const UserID& user_id, const std::string& s)
{
	Lock lock(mutex);
	user_web_messages[user_id] = s;
}


std::string ServerAllWorldsState::getAndRemoveUserWebMessage(const UserID& user_id) // returns empty string if no message or user
{
	Lock lock(mutex);
	auto res = user_web_messages.find(user_id);
	if(res != user_web_messages.end())
	{
		const std::string msg = res->second;
		user_web_messages.erase(res);
		return msg;
	}
	else
		return std::string();
}
