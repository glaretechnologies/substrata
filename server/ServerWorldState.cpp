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
#include <BufferViewInStream.h>
#include "../shared/LODChunk.h"


static const uint32 SERVER_SINGLE_WORLD_STATE_SERIALISATON_VERSION = 1;


void ServerWorldState::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(SERVER_SINGLE_WORLD_STATE_SERIALISATON_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	::writeToStream(details.owner_id, stream);
	details.created_time.writeToStream(stream);
	stream.writeStringLengthFirst(details.name);
	stream.writeStringLengthFirst(details.description);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readServerWorldStateFromStream(RandomAccessInStream& stream, ServerWorldState& world)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readServerWorldStateFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readServerWorldStateFromStream: buffer_size was too large");

	world.details.owner_id = readUserIDFromStream(stream);
	world.details.created_time.readFromStream(stream);
	world.details.name = stream.readStringLengthFirst(WorldDetails::MAX_NAME_SIZE);
	world.details.description = stream.readStringLengthFirst(WorldDetails::MAX_DESCRIPTION_SIZE);

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


ServerAllWorldsState::ServerAllWorldsState()
:	lua_vms(/*empty key=*/UserID::invalidUserID())
{
	migration_version_info.migration_version = 0;

	next_avatar_uid = UID(0);
	next_object_uid = UID(0);
	next_order_uid = 0;
	next_sub_eth_transaction_uid = 0;

	setWorldState(/*world name=*/"", new ServerWorldState());

	last_parcel_update_info.last_parcel_sale_update_hour = 0;
	last_parcel_update_info.last_parcel_sale_update_day = 0;
	last_parcel_update_info.last_parcel_sale_update_year = 0;

	BTC_per_EUR = 0;
	ETH_per_EUR = 0;

	eth_info.min_next_nonce = 0;

	server_admin_message_changed = false;

	read_only_mode = false;

	force_dyn_tex_update = false;
}


ServerAllWorldsState::~ServerAllWorldsState()
{
	root_world_state = nullptr;

	// Delete any objects first, which may contain references to Lua stuff, before we delete the Lua VMs
	world_states.clear();

	lua_vms.clear();
}


void ServerAllWorldsState::createNewDatabase(const std::string& path)
{
	conPrint("Creating new world state database at '" + path + "'...");

	Lock lock(mutex);

	database.openAndMakeOrClearDatabase(path);
}


static const uint32 WORLD_STATE_MAGIC_NUMBER = 487173571;
static const uint32 WORLD_STATE_SERIALISATION_VERSION = 3; // v3: using Database
static const uint32 WORLD_CHUNK = 50;
static const uint32 WORLD_SETTINGS_CHUNK = 60;
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
static const uint32 NEWS_POST_CHUNK = 112;
static const uint32 FEATURE_FLAG_CHUNK = 113;
static const uint32 OBJECT_STORAGE_ITEM_CHUNK = 114;
static const uint32 USER_SECRET_CHUNK = 115;
static const uint32 LOD_CHUNK_CHUNK = 116;
static const uint32 SUB_EVENT_CHUNK = 117;
static const uint32 MIGRATION_VERSION_CHUNK = 118;
static const uint32 PHOTO_CHUNK = 119;
static const uint32 EOS_CHUNK = 1000;


static const uint32 PARCEL_SALE_UPDATE_VERSION = 1;
static const uint32 MAP_TILE_INFO_VERSION = 1;
static const uint32 ETH_INFO_CHUNK_VERSION = 1;
static const uint32 FEATURE_FLAG_CHUNK_VERSION = 1;
static const uint32 OBJECT_STORAGE_ITEM_VERSION = 1;
static const uint32 USER_SECRET_VERSION = 1;
static const uint32 MIGRATION_VERSION_CHUNK_VERSION = 1;


void ServerAllWorldsState::readFromDisk(const std::string& path)
{
	conPrint("Reading world state from '" + path + "'...");

	WorldStateLock lock(mutex);

	Timer timer;

	size_t num_obs = 0;
	size_t num_parcels = 0;
	size_t num_orders = 0;
	size_t num_sessions = 0;
	size_t num_auctions = 0;
	size_t num_screenshots = 0;
	size_t num_sub_eth_transactions = 0;
	size_t num_tiles_read = 0;
	size_t num_world_settings = 0;
	size_t num_news_posts = 0;
	size_t num_object_storage_items = 0;
	size_t num_user_secrets = 0;
	size_t num_lod_chunks = 0;
	size_t num_events = 0;
	size_t num_resources = 0;
	size_t num_worlds = 0;
	size_t num_photos = 0;

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

		for(auto it = database.getRecordMap().begin(); it != database.getRecordMap().end(); ++it)
		{
			const DatabaseKey database_key = it->first;
			const Database::RecordInfo& record = it->second;

			if(record.isRecordValid())
			{
				BufferViewInStream stream(ArrayRef<uint8>(database.getInitialRecordData(record), record.len));

				// Now deserialise from our temp buffer
				const uint32 chunk = stream.readUInt32();
				if(chunk == WORLD_CHUNK)
				{
					ServerWorldStateRef world = new ServerWorldState();
					readServerWorldStateFromStream(stream, *world);

					// See if we have already created this world object while reading a WORLD_OBJECT_CHUNK, PARCEL_CHUNK etc. below
					auto res = world_states.find(world->details.name);
					if(res == world_states.end())
					{
						// World is not created and inserted into world_states yet:
						
						world->database_key = database_key;

						setWorldState(/*world name=*/world->details.name, world);
					}
					else
					{
						// World object has already been created and inserted into world_states.
						// In this case just copy over the properties we read from disk and the database key.
						ServerWorldStateRef existing_world = res->second;
						existing_world->details = world->details;
						existing_world->database_key = database_key;
					}
					
					num_worlds++;
				}
				else if(chunk == WORLD_OBJECT_CHUNK)
				{
					// Read world name
					const std::string world_name = stream.readStringLengthFirst(10000);

					// Create ServerWorldState for world name if needed
					if(world_states.count(world_name) == 0) 
						setWorldState(/*world name=*/world_name, new ServerWorldState());

					// Deserialise object
					WorldObjectRef world_ob = new WorldObject();
					readWorldObjectFromStream(stream, *world_ob);

					//TEMP HACK: clear lightmap needed flag
					BitUtils::zeroBit(world_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);

					world_ob->database_key = database_key;
					world_states[world_name]->getObjects(lock)[world_ob->uid] = world_ob; // Add to object map
					num_obs++;

					next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
				}
				else if(chunk == USER_CHUNK)
				{
					// Deserialise user
					UserRef user = new User();
					readUserFromStream(stream, *user);

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
						setWorldState(/*world name=*/world_name, new ServerWorldState());

					// Deserialise parcel
					ParcelRef parcel = new Parcel();
					readFromStream(stream, *parcel);

					parcel->database_key = database_key;
					world_states[world_name]->getParcels(lock)[parcel->id] = parcel; // Add to parcel map
					num_parcels++;
				}
				else if(chunk == WORLD_SETTINGS_CHUNK)
				{
					// Read world name
					const std::string world_name = stream.readStringLengthFirst(10000);

					// Create ServerWorldState for world name if needed
					if(world_states.count(world_name) == 0)
						setWorldState(/*world name=*/world_name, new ServerWorldState());

					// NOTE: There was a bug with multiple world settings for the same world getting saved to the database.  Resolve ambiguity of which one to use by choosing the setting with the largest database key value.
					// Use these new settings iff the existing settings are either uninitialised (in which case database_key will be invalid), or the settings we are reading from the DB have a greater key 
					// value than the existing settings.
					const bool use_settings = !world_states[world_name]->world_settings.database_key.valid() || (database_key.value() > world_states[world_name]->world_settings.database_key.value());
					if(use_settings)
					{	
						// Deserialise world settings
						readWorldSettingsFromStream(stream, world_states[world_name]->world_settings);

						world_states[world_name]->world_settings.database_key = database_key;
					}

					num_world_settings++;
				}
				else if(chunk == RESOURCE_CHUNK)
				{
					// Deserialise resource
					ResourceRef resource = new Resource();
					const uint32 res_version = readFromStream(stream, *resource);
					
					// Resource serialisation version 3 added serialisation of resource state.  If we are reading a resource before that, just assume it is present on disk,
					// which is what addResource() below used to do.
					if(res_version < 3)
						resource->setState(Resource::State_Present);

					//conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getRawLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

					resource->database_key = database_key;
					this->resource_manager->addResource(resource);

					num_resources++;
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
					readScreenshotFromStream(stream, *shot);

					shot->database_key = database_key;
					screenshots[shot->id] = shot;
					num_screenshots++;
				}
				else if(chunk == PHOTO_CHUNK)
				{
					// Deserialise Photo
					PhotoRef photo = new Photo();
					readPhotoFromStream(stream, *photo);

					photo->database_key = database_key;
					photos[photo->id] = photo;
					num_photos++;
				}
				else if(chunk == SUB_ETH_TRANSACTIONS_CHUNK)
				{
					// Deserialise SubEthTransaction
					SubEthTransactionRef trans = new SubEthTransaction();
					readFromStream(stream, *trans);

					next_sub_eth_transaction_uid = myMax(trans->id + 1, next_sub_eth_transaction_uid);

					trans->database_key = database_key;
					sub_eth_transactions[trans->id] = trans;
					num_sub_eth_transactions++;
				}
				else if(chunk == NEWS_POST_CHUNK)
				{
					// Deserialise NewsPost
					NewsPostRef post = new NewsPost();
					readNewsPostFromStream(stream, *post);

					post->database_key = database_key;
					news_posts[post->id] = post;
					num_news_posts++;
				}
				else if(chunk == OBJECT_STORAGE_ITEM_CHUNK)
				{
					// Deserialise ObjectStorageItem
					const uint32 item_version = stream.readUInt32();
					if(item_version != OBJECT_STORAGE_ITEM_VERSION)
						throw glare::Exception("invalid object storage item version: " + toString(item_version));

					ObjectStorageItemRef item = new ObjectStorageItem();

					// Read key
					item->key.ob_uid = readUIDFromStream(stream);
					item->key.key_string = stream.readStringLengthFirst(1000);

					// Read size of data
					const uint32 data_size = stream.readUInt32();
					if(data_size > (1 << 16))
						throw glare::Exception("Invalid object storage data size: " + toString(data_size));

					// Read data
					item->data.resizeNoCopy(data_size);
					stream.readData(item->data.data(), data_size);

					item->database_key = database_key;
					object_storage_items[item->key] = item;
					object_num_storage_items[item->key.ob_uid]++;
					num_object_storage_items++;
				}
				else if(chunk == USER_SECRET_CHUNK)
				{
					// Deserialise UserSecret
					const uint32 user_secret_version = stream.readUInt32();
					if(user_secret_version != USER_SECRET_VERSION)
						throw glare::Exception("invalid user secret version: " + toString(user_secret_version));

					UserSecretRef secret = new UserSecret();

					// Read key
					secret->key.user_id = readUserIDFromStream(stream);
					secret->key.secret_name = stream.readStringLengthFirst(UserSecret::MAX_SECRET_NAME_SIZE);
					
					secret->value = stream.readStringLengthFirst(UserSecret::MAX_VALUE_SIZE);

					secret->database_key = database_key;
					user_secrets[secret->key] = secret;
					num_user_secrets++;
				}
				else if(chunk == ETH_INFO_CHUNK)
				{
					const uint32 eth_info_v = stream.readInt32();
					if(eth_info_v != ETH_INFO_CHUNK_VERSION)
						throw glare::Exception("invalid eth_info version: " + toString(eth_info_v));

					this->eth_info.database_key = database_key;
					this->eth_info.min_next_nonce = stream.readInt32();
				}
				else if(chunk == FEATURE_FLAG_CHUNK)
				{
					const uint32 ff_info_v = stream.readInt32();
					if(ff_info_v != FEATURE_FLAG_CHUNK_VERSION)
						throw glare::Exception("invalid feature flag version: " + toString(ff_info_v));

					this->feature_flag_info.database_key = database_key;

					this->feature_flag_info.feature_flags = stream.readUInt64();
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
							readScreenshotFromStream(stream, *tile_info.cur_tile_screenshot);
						}
						const bool prev_tile_screenshot_non_null = stream.readInt32() != 0;
						if(prev_tile_screenshot_non_null)
						{
							tile_info.prev_tile_screenshot = new Screenshot();
							readScreenshotFromStream(stream, *tile_info.prev_tile_screenshot);
						}

						map_tile_info.info[Vec3<int>(x, y, z)] = tile_info; // Insert
					}

					map_tile_info.database_key = database_key;

					num_tiles_read = num_tiles;
				}
				else if(chunk == LOD_CHUNK_CHUNK)
				{
					// Read world name
					const std::string world_name = stream.readStringLengthFirst(10000);

					// Create ServerWorldState for world name if needed
					if(world_states.count(world_name) == 0)
						setWorldState(/*world name=*/world_name, new ServerWorldState());

					Reference<LODChunk> lod_chunk = new LODChunk();

					readLODChunkFromStream(stream, *lod_chunk);
					
					lod_chunk->database_key = database_key;
					world_states[world_name]->getLODChunks(lock)[lod_chunk->coords] = lod_chunk;
					num_lod_chunks++;
				}
				else if(chunk == SUB_EVENT_CHUNK)
				{
					// Deserialise SubEvent
					SubEventRef event = new SubEvent();
					readSubEventFromStream(stream, *event);

					event->database_key = database_key;
					events[event->id] = event;
					num_events++;
				}
				else if(chunk == MIGRATION_VERSION_CHUNK)
				{
					const uint32 chunk_version = stream.readInt32();
					if(chunk_version != MIGRATION_VERSION_CHUNK_VERSION)
						throw glare::Exception("invalid migration version chunk version: " + toString(chunk_version));

					this->migration_version_info.migration_version = stream.readUInt32();

					this->migration_version_info.database_key = database_key;
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
		setWorldState(/*world name=*/"", current_world);

		FileInStream stream(path);

		// Read magic number
		const uint32 m = stream.readUInt32();
		if(m != WORLD_STATE_MAGIC_NUMBER)
			throw glare::Exception("Invalid magic number " + toString(m) + ", expected " + toString(WORLD_STATE_MAGIC_NUMBER) + ".");

		// Read version
		const uint32 version = stream.readUInt32();
		if(version > WORLD_STATE_SERIALISATION_VERSION)
			throw glare::Exception("Unknown version " + toString(version) + ", expected " + toString(WORLD_STATE_SERIALISATION_VERSION) + ".");

		while(1)
		{
			const uint32 chunk = stream.readUInt32();
			if(chunk == WORLD_CHUNK)
			{
				const std::string world_name = stream.readStringLengthFirst(1000);
				if(world_states.count(world_name) == 0)
					setWorldState(/*world name=*/world_name, current_world);

				current_world = world_states[world_name];
			}
			else if(chunk == WORLD_OBJECT_CHUNK)
			{
				// Deserialise object
				WorldObjectRef world_ob = new WorldObject();
				readWorldObjectFromStream(stream, *world_ob);

				//TEMP HACK: clear lightmap needed flag
				BitUtils::zeroBit(world_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);

				current_world->getObjects(lock)[world_ob->uid] = world_ob; // Add to object map
				num_obs++;

				next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
			}
			else if(chunk == USER_CHUNK)
			{
				// Deserialise user
				UserRef user = new User();
				readUserFromStream(stream, *user);

				user_id_to_users[user->id] = user; // Add to user map
				name_to_users[user->name] = user; // Add to user map
			}
			else if(chunk == PARCEL_CHUNK)
			{
				// Deserialise parcel
				ParcelRef parcel = new Parcel();
				readFromStream(stream, *parcel);

				current_world->getParcels(lock)[parcel->id] = parcel; // Add to parcel map
				num_parcels++;
			}
			else if(chunk == RESOURCE_CHUNK)
			{
				// Deserialise resource
				ResourceRef resource = new Resource();
				readFromStream(stream, *resource);

				//conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

				this->resource_manager->addResource(resource);

				num_resources++;
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
				readScreenshotFromStream(stream, *shot);

				screenshots[shot->id] = shot;
				num_screenshots++;
			}
			else if(chunk == SUB_ETH_TRANSACTIONS_CHUNK)
			{
				// Deserialise SubEthTransaction
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
						readScreenshotFromStream(stream, *tile_info.cur_tile_screenshot);
					}
					const bool prev_tile_screenshot_non_null = stream.readInt32() != 0;
					if(prev_tile_screenshot_non_null)
					{
						tile_info.prev_tile_screenshot = new Screenshot();
						readScreenshotFromStream(stream, *tile_info.prev_tile_screenshot);
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

		this->migration_version_info.migration_version = 0;
	}


	// If we were loading the old pre-database format:
	if(is_pre_database_format)
	{
		database.openAndMakeOrClearDatabase(path);

		// Add everything to dirty sets so it gets saved to the DB initially.
		addEverythingToDirtySets();
	}


	doMigrations(lock);

	denormaliseData();

	// Compress voxel data if needed.
	// for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	// {
	// 	Reference<ServerWorldState> world_state = world_it->second;
	// 	for(auto it = world_state->getObjects(lock).begin(); it != world_state->getObjects(lock).end(); ++it)
	// 	{
	// 		/*WorldObject* ob = it->second.ptr();
	// 		if(!ob->voxel_group.voxels.empty() && ob->compressed_voxels.empty())
	// 		{
	// 			WorldObject::compressVoxelGroup(ob->voxel_group, ob->compressed_voxels);
	// 		}*/
	// 		//ob->compressVoxels();
	// 	}
	// }

	//conPrint("min_next_nonce: " + toString(eth_info.min_next_nonce));
	conPrint("Loaded " + toString(num_obs) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " +
		toString(num_parcels) + " parcel(s), " + toString(num_resources) + " resource(s), " + toString(num_orders) + " order(s), " + 
		toString(num_sessions) + " session(s), " + toString(num_auctions) + " auction(s), " + toString(num_screenshots) + " screenshot(s), " + 
		toString(num_sub_eth_transactions) + " sub eth transaction(s), " + toString(num_tiles_read) + " tiles, " + toString(num_world_settings) + " world settings, " + 
		toString(num_news_posts) + " news posts, " + toString(num_object_storage_items) + " object storage item(s), " + toString(num_user_secrets) + " user secret(s), " + 
		toString(num_lod_chunks) + " lod chunk(s), " + toString(num_events) + " event(s), " + toString(num_photos) + " photo(s) in " + timer.elapsedStringNSigFigs(4));
}


void ServerAllWorldsState::addEverythingToDirtySets()
{
	WorldStateLock lock(mutex);

	{
		Lock resource_manager_lock(resource_manager->getMutex());
		for(auto it = resource_manager->getResourcesForURL().begin(); it != resource_manager->getResourcesForURL().end(); ++it)
			db_dirty_resources.insert(it->second);
	}

	for(auto it = user_id_to_users.begin(); it != user_id_to_users.end(); ++it)
		db_dirty_users.insert(it->second);

	for(auto it = orders.begin(); it != orders.end(); ++it)
		db_dirty_orders.insert(it->second);

	for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	{
		Reference<ServerWorldState> world_state = world_it->second;

		for(auto it = world_state->getObjects(lock).begin(); it != world_state->getObjects(lock).end(); ++it)
			world_state->getDBDirtyWorldObjects(lock).insert(it->second);

		for(auto it = world_state->getParcels(lock).begin(); it != world_state->getParcels(lock).end(); ++it)
			world_state->getDBDirtyParcels(lock).insert(it->second);
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


bool ServerAllWorldsState::isInReadOnlyMode()
{ 
	Lock lock(mutex); 
	return read_only_mode; 
}


void ServerAllWorldsState::clearAndReset() // Just for fuzzing
{
	Lock lock(mutex);
	next_object_uid = UID(0);
	next_avatar_uid = UID(0);
}


void ServerAllWorldsState::addPersonalWorldForUser(const UserRef user, WorldStateLock& /*lock*/)
{
	ServerWorldStateRef personal_world = new ServerWorldState();
	personal_world->details.owner_id = user->id;
	personal_world->details.created_time = user->created_time;
	personal_world->details.name = user->name;
	personal_world->details.description = user->name + "'s personal world";
	personal_world->db_dirty = true;

	world_states[personal_world->details.name] = personal_world;
}


void ServerAllWorldsState::denormaliseData()
{
	WorldStateLock lock(mutex);

	for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
	{
		Reference<ServerWorldState> world_state = world_it->second;

		// Build cached fields like WorldObject::creator_name
		for(auto i=world_state->getObjects(lock).begin(); i != world_state->getObjects(lock).end(); ++i)
		{
			auto res = user_id_to_users.find(i->second->creator_id);
			if(res != user_id_to_users.end())
				i->second->creator_name = res->second->name;
		}

		for(auto i=world_state->getParcels(lock).begin(); i != world_state->getParcels(lock).end(); ++i)
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


/*

Migration version history
-------------------------
1: Set AUDIO_AUTOPLAY and AUDIO_LOOP flags on all WorldObjects with non-empty audio_source_url, since that was the behaviour before the flags were introduced.
3: Make main world and personal world ServerWorldState objects for all users.

*/
void ServerAllWorldsState::doMigrations(WorldStateLock& lock)
{
	if(this->migration_version_info.migration_version < 1)
	{
		conPrint("Doing DB migration from version " + toString(migration_version_info.migration_version) + ": Setting AUDIO_AUTOPLAY and AUDIO_LOOP flags.");

		for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
		{
			ServerWorldState* world_state = world_it->second.ptr();
			for(auto it = world_state->getObjects(lock).begin(); it != world_state->getObjects(lock).end(); ++it)
			{
				WorldObject* ob = it->second.ptr();
				if(!ob->audio_source_url.empty())
				{
					ob->flags |= (WorldObject::AUDIO_AUTOPLAY | WorldObject::AUDIO_LOOP);
					world_state->addWorldObjectAsDBDirty(ob, lock);
				}
			}
		}
	}

	if(this->migration_version_info.migration_version < 3)
	{
		conPrint("Doing DB migration from version " + toString(migration_version_info.migration_version) + ": Make main world and personal world ServerWorldState objects for all users.");
		size_t num_worlds_added = 0;

		// Create main world if not already present.
		if(world_states.count("") == 0)
		{
			ServerWorldStateRef world = new ServerWorldState();
			world->details.created_time = TimeStamp::currentTime();
			world->details.owner_id = UserID(0);
			world->details.name = "";
			world->details.description = "Main world";
			world->db_dirty = true;

			world_states[""] = world;
			root_world_state = world;
			num_worlds_added++;
		}

		// Create personal worlds for all users if not already present.
		for(auto it = user_id_to_users.begin(); it != user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			const std::string world_name = user->name;
			if(world_states.count(world_name) == 0)
			{
				ServerWorldStateRef world = new ServerWorldState();
				world->details.created_time = TimeStamp::currentTime();
				world->details.owner_id = user->id;
				world->details.name = world_name;
				world->details.description = user->name + "'s personal world";
				world->db_dirty = true;

				world_states[world_name] = world;
				num_worlds_added++;
			}
		}

		conPrint("Added " + toString(num_worlds_added) + " worlds.");
	}

	if(this->migration_version_info.migration_version < 4)
	{
		conPrint("Doing DB migration from version " + toString(migration_version_info.migration_version) + ": Make main world and personal world ServerWorldState objects for all users (round 2).");
		size_t num_worlds_added = 0;
		size_t num_worlds_updated = 0;

		// Create main world if not already present, If the main world is present in world_states but not initialised properly with owner_id etc.., update the world in world_states.
		{
			auto res = world_states.find("");
			if(res == world_states.end())
			{
				ServerWorldStateRef world = new ServerWorldState();
				world->details.created_time = TimeStamp::currentTime();
				world->details.owner_id = UserID(0);
				world->details.name = "";
				world->details.description = "Main world";
				world->db_dirty = true;

				world_states[""] = world;
				root_world_state = world;
				num_worlds_added++;
			}
			else
			{
				ServerWorldStateRef world = res->second;
				if(!world->details.owner_id.valid())
				{
					world->details.created_time = TimeStamp::currentTime();
					world->details.owner_id = UserID(0);
					world->details.name = "";
					world->details.description = "Main world";
					world->db_dirty = true;
					num_worlds_updated++;
				}
			}
		}

		// Create personal worlds for all users if not already present.
		for(auto it = user_id_to_users.begin(); it != user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			const std::string world_name = user->name;
			auto res = world_states.find(world_name);
			if(res == world_states.end())
			{
				ServerWorldStateRef world = new ServerWorldState();
				world->details.created_time = TimeStamp::currentTime();
				world->details.owner_id = user->id;
				world->details.name = world_name;
				world->details.description = user->name + "'s personal world";
				world->db_dirty = true;

				world_states[world_name] = world;
				num_worlds_added++;
			}
			else
			{
				ServerWorldStateRef world = res->second;
				if(!world->details.owner_id.valid())
				{
					world->details.created_time = TimeStamp::currentTime();
					world->details.owner_id = user->id;
					world->details.name = world_name;
					world->details.description = user->name + "'s personal world";
					world->db_dirty = true;
					num_worlds_updated++;
				}
			}
		}

		conPrint("Added " + toString(num_worlds_added) + " worlds, updated " + toString(num_worlds_updated) + " worlds.");
	}


	if(this->migration_version_info.migration_version < 4)
	{
		this->migration_version_info.migration_version = 4;
		this->migration_version_info.db_dirty = true;
		this->changed = 1;
	}
}


// Removes sensitive information from the database, such as user passwords, email addresses, billing information, web sessions etc.
// Then saves the updates to disk.
void ServerAllWorldsState::saveSanitisedDatabase()
{
	conPrint("Saving sanitised world state to disk...");

	WorldStateLock lock(mutex);

	try
	{
		// Clear some sensitive fields (passwords etc.), and just delete some sensitive object types (SubEthTransactions).

		// For each world
		for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
		{
			Reference<ServerWorldState> world_state = world_it->second;

			// Sanitise parcels
			for(auto it = world_state->getParcels(lock).begin(); it != world_state->getParcels(lock).end(); ++it)
			{
				Parcel* parcel = it->second.ptr();
				parcel->minting_transaction_id = std::numeric_limits<uint64>::max();
				parcel->parcel_auction_ids.clear();

				world_state->getDBDirtyParcels(lock).insert(parcel); // Mark parcel as dirty
			}
		}

		// Sanitise users
		{
			int i = 0;
			for(auto it=user_id_to_users.begin(); it != user_id_to_users.end(); ++it)
			{
				User* user = it->second.ptr();
				user->name = "User " + toString(i); // Replace name and email address with something generic.
				user->email_address = "user_" + toString(i) + "@email.com";
				user->hashed_password.clear();
				user->password_hash_salt.clear();
				user->controlled_eth_address = "";
				user->password_resets.clear();

				user->setNewPasswordAndSalt("aaaaaaaa"); // Set to (the hash of) a known password, so we can log in as e.g. user 0 for testing.

				db_dirty_users.insert(user); // Mark as dirty

				i++;
			}
		}

		// resource objects
		{
			/*for(auto i=db_dirty_resources.begin(); i != db_dirty_resources.end(); ++i)
			{
				Resource* resource = i->ptr();
			}*/
		}

		// Sanitise orders
		{
			for(auto i=orders.begin(); i != orders.end(); ++i)
			{
				Order* order = i->second.ptr();
				order->payer_email = "";
				order->gross_payment = 0;
				order->currency = "";
				order->paypal_data = "";
				order->coinbase_charge_code = "";
				order->coinbase_status = "";

				db_dirty_orders.insert(order);
			}
		}

		// Delete all UserWebSessions
		{
			for(auto i=user_web_sessions.begin(); i != user_web_sessions.end(); ++i)
			{
				UserWebSession* session = i->second.ptr();
				assert(session->database_key.valid());
				
				db_records_to_delete.insert(session->database_key);
			}
		}

		// Delete all ParcelAuctions for now
		{
			for(auto i=parcel_auctions.begin(); i != parcel_auctions.end(); ++i)
			{
				ParcelAuction* auction = i->second.ptr();
				assert(auction->database_key.valid());

				db_records_to_delete.insert(auction->database_key);
			}
		}

		// Screenshots
		{
			/*for(auto it=screenshots.begin(); it != screenshots.end(); ++it)
			{
				Screenshot* shot = it->second.ptr();
			}*/
		}

		// Delete all SubEthTransactions
		{
			for(auto i=sub_eth_transactions.begin(); i != sub_eth_transactions.end(); ++i)
			{
				SubEthTransaction* trans = i->second.ptr();
				assert(trans->database_key.valid());

				db_records_to_delete.insert(trans->database_key);
			}
		}

		// MAP_TILE_INFO_CHUNK
		
		// LAST_PARCEL_SALE_UPDATE_CHUNK

		// ETH_INFO_CHUNK

		// Write to disk.  Will do the updates we have added to dirty sets, and delete records we have added to db_records_to_delete.
		serialiseToDisk(lock);
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


// Write any changed data (objects in dirty set) to disk.  Mutex should be held already.
void ServerAllWorldsState::serialiseToDisk(WorldStateLock& lock)
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
		size_t num_world_settings = 0;
		size_t num_news_posts = 0;
		size_t num_object_storage_items = 0;
		size_t num_user_secrets = 0;
		size_t num_lod_chunks = 0;
		size_t num_events = 0;
		size_t num_worlds = 0;
		size_t num_photos = 0;

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
			const std::string& world_name = world_it->first;
			ServerWorldState* world_state = world_it->second.ptr();

			// Write world (if dirty)
			if(world_state->db_dirty)
			{
				temp_buf.clear();
				temp_buf.writeUInt32(WORLD_CHUNK);
				world_state->writeToStream(temp_buf); // Write world

				if(!world_state->database_key.valid())
					world_state->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(world_state->database_key, temp_buf.buf);

				world_state->db_dirty = false;

				num_worlds++;
			}

			// Write objects
			{
				for(auto it = world_state->getDBDirtyWorldObjects(lock).begin(); it != world_state->getDBDirtyWorldObjects(lock).end(); ++it)
				{
					WorldObject* ob = it->ptr();
					temp_buf.clear();
					temp_buf.writeUInt32(WORLD_OBJECT_CHUNK);
					temp_buf.writeStringLengthFirst(world_name); // Write world name
					ob->writeToStream(temp_buf); // Write object

					if(!ob->database_key.valid())
						ob->database_key = database.allocUnusedKey(); // Get a new key

					database.updateRecord(ob->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

					num_obs++;
				}

				world_state->getDBDirtyWorldObjects(lock).clear();
			}

			// Write parcels
			{
				for(auto it = world_state->getDBDirtyParcels(lock).begin(); it != world_state->getDBDirtyParcels(lock).end(); ++it)
				{
					Parcel* parcel = it->ptr();
					temp_buf.clear();
					temp_buf.writeUInt32(PARCEL_CHUNK);
					temp_buf.writeStringLengthFirst(world_name); // Write world name
					writeToStream(*parcel, temp_buf); // Write parcel

					if(!parcel->database_key.valid())
						parcel->database_key = database.allocUnusedKey(); // Get a new key

					database.updateRecord(parcel->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

					num_parcels++;
				}

				world_state->getDBDirtyParcels(lock).clear();
			}

			// Write LODChunks
			{
				for(auto it = world_state->getDBDirtyLODChunks(lock).begin(); it != world_state->getDBDirtyLODChunks(lock).end(); ++it)
				{
					LODChunk* chunk = it->ptr();
					temp_buf.clear();
					temp_buf.writeUInt32(LOD_CHUNK_CHUNK);
					temp_buf.writeStringLengthFirst(world_name); // Write world name
					chunk->writeToStream(temp_buf);

					if(!chunk->database_key.valid())
						chunk->database_key = database.allocUnusedKey(); // Get a new key

					database.updateRecord(chunk->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

					num_lod_chunks++;
				}

				world_state->getDBDirtyLODChunks(lock).clear();
			}

			// Save the world settings if dirty
			if(world_state->world_settings.db_dirty)
			{
				temp_buf.clear();
				temp_buf.writeUInt32(WORLD_SETTINGS_CHUNK);
				temp_buf.writeStringLengthFirst(world_name); // Write world name
				world_state->world_settings.writeToStream(temp_buf); // Write world settings to temp_buf

				if(!world_state->world_settings.database_key.valid())
					world_state->world_settings.database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(world_state->world_settings.database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				world_state->world_settings.db_dirty = false;

				num_world_settings++;
			}
		} // End for each world

		// Write users
		{
			for(auto it=db_dirty_users.begin(); it != db_dirty_users.end(); ++it)
			{
				User* user = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(USER_CHUNK);
				writeUserToStream(*user, temp_buf);

				if(!user->database_key.valid())
					user->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(user->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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
				resource->writeToStream(temp_buf);

				if(!resource->database_key.valid())
					resource->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(resource->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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

				database.updateRecord(order->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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

				database.updateRecord(session->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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

				database.updateRecord(auction->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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
				writeScreenshotToStream(*shot, temp_buf);

				if(!shot->database_key.valid())
					shot->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(shot->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				num_screenshots++;
			}

			db_dirty_screenshots.clear();
		}

		// Write Photos
		{
			for(auto it=db_dirty_photos.begin(); it != db_dirty_photos.end(); ++it)
			{
				Photo* photo = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(PHOTO_CHUNK);
				photo->writeToStream(temp_buf);

				if(!photo->database_key.valid())
					photo->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(photo->database_key, ArrayRef<uint8>(temp_buf.buf));

				num_photos++;
			}

			db_dirty_photos.clear();
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

				database.updateRecord(trans->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				num_sub_eth_transactions++;
			}

			db_dirty_sub_eth_transactions.clear();
		}
		
		// Write NewsPosts
		{
			for(auto it=db_dirty_news_posts.begin(); it != db_dirty_news_posts.end(); ++it)
			{
				NewsPost* post = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(NEWS_POST_CHUNK);
				writeToStream(*post, temp_buf);

				if(!post->database_key.valid())
					post->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(post->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				num_news_posts++;
			}

			db_dirty_news_posts.clear();
		}

		// Write ObjectStorageItems
		{
			for(auto it=db_dirty_object_storage_items.begin(); it != db_dirty_object_storage_items.end(); ++it)
			{
				ObjectStorageItem* item = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(OBJECT_STORAGE_ITEM_CHUNK);
				temp_buf.writeUInt32(OBJECT_STORAGE_ITEM_VERSION);

				// Write key
				writeToStream(item->key.ob_uid, temp_buf);
				temp_buf.writeStringLengthFirst(item->key.key_string);

				temp_buf.writeUInt32((uint32)item->data.size()); // Write size of data
				temp_buf.writeData(item->data.data(), item->data.size()); // Write data

				if(!item->database_key.valid())
					item->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(item->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				num_object_storage_items++;
			}

			db_dirty_object_storage_items.clear();
		}

		// Write UserSecrets
		{
			for(auto it=db_dirty_user_secrets.begin(); it != db_dirty_user_secrets.end(); ++it)
			{
				UserSecret* secret = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(USER_SECRET_CHUNK);
				temp_buf.writeUInt32(USER_SECRET_VERSION);

				// Write key
				writeToStream(secret->key.user_id, temp_buf);
				temp_buf.writeStringLengthFirst(secret->key.secret_name);

				temp_buf.writeStringLengthFirst(secret->value); // Write value

				if(!secret->database_key.valid())
					secret->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(secret->database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

				num_user_secrets++;
			}

			db_dirty_user_secrets.clear();
		}

		// Write SubEvents
		{
			for(auto it = db_dirty_events.begin(); it != db_dirty_events.end(); ++it)
			{
				SubEvent* event = it->ptr();
				temp_buf.clear();
				temp_buf.writeUInt32(SUB_EVENT_CHUNK);
				event->writeToStream(temp_buf);

				if(!event->database_key.valid())
					event->database_key = database.allocUnusedKey(); // Get a new key

				database.updateRecord(event->database_key, temp_buf.buf);

				num_events++;
			}

			db_dirty_events.clear();
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
					writeScreenshotToStream(*tile_info.cur_tile_screenshot, temp_buf);

				temp_buf.writeInt32(tile_info.prev_tile_screenshot.nonNull() ? 1 : 0);
				if(tile_info.prev_tile_screenshot.nonNull())
					writeScreenshotToStream(*tile_info.prev_tile_screenshot, temp_buf);
			}

			if(!map_tile_info.database_key.valid())
				map_tile_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(map_tile_info.database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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

			database.updateRecord(last_parcel_update_info.database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

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

			database.updateRecord(eth_info.database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

			eth_info.db_dirty = false;
		}

		// Write FEATURE_FLAG_CHUNK
		if(feature_flag_info.db_dirty)
		{
			temp_buf.clear();
			temp_buf.writeUInt32(FEATURE_FLAG_CHUNK);
			temp_buf.writeUInt32(FEATURE_FLAG_CHUNK_VERSION);
			temp_buf.writeUInt64(feature_flag_info.feature_flags);

			if(!feature_flag_info.database_key.valid())
				feature_flag_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(feature_flag_info.database_key, ArrayRef<uint8>(temp_buf.buf.data(), temp_buf.buf.size()));

			feature_flag_info.db_dirty = false;
		}
		
		// Write MIGRATION_VERSION_CHUNK
		if(migration_version_info.db_dirty)
		{
			temp_buf.clear();
			temp_buf.writeUInt32(MIGRATION_VERSION_CHUNK);
			temp_buf.writeUInt32(MIGRATION_VERSION_CHUNK_VERSION);
			temp_buf.writeUInt32(migration_version_info.migration_version);

			if(!migration_version_info.database_key.valid())
				migration_version_info.database_key = database.allocUnusedKey(); // Get a new key

			database.updateRecord(migration_version_info.database_key, ArrayRef<uint8>(temp_buf.buf));

			migration_version_info.db_dirty = false;

			conPrint("Saved new DB migration version: " + toString(migration_version_info.migration_version));
		}

		database.flush();

		std::string msg = "Saved ";
		if(num_worlds > 0)                msg += toString(num_worlds) + " world(s), ";
		if(num_obs > 0)                   msg += toString(num_obs) +   " object(s), ";
		if(num_users > 0)                 msg += toString(num_users) + " user(s), ";
		if(num_parcels > 0)               msg += toString(num_parcels) + " parcels(s), ";
		if(num_resources > 0)             msg += toString(num_resources) + " resources(s), ";
		if(num_orders > 0)                msg += toString(num_orders) + " orders(s), ";
		if(num_sessions > 0)              msg += toString(num_sessions) + " sessions(s), ";
		if(num_auctions > 0)              msg += toString(num_auctions) + " auctions(s), ";
		if(num_screenshots > 0)           msg += toString(num_screenshots) + " screenshots(s), ";
		if(num_sub_eth_transactions > 0)  msg += toString(num_sub_eth_transactions) + " sub eth transactions(s), ";
		if(num_tiles_written > 0)         msg += toString(num_tiles_written) + " tiles(s), ";
		if(num_world_settings > 0)        msg += toString(num_world_settings) + " world setting(s), ";
		if(num_news_posts > 0)            msg += toString(num_news_posts) + " news post(s), ";
		if(num_object_storage_items > 0)  msg += toString(num_object_storage_items) + " object storage item(s), ";
		if(num_user_secrets > 0)          msg += toString(num_user_secrets) + " user secret(s), ";
		if(num_lod_chunks > 0)            msg += toString(num_lod_chunks) + " LOD chunk(s), ";
		if(num_events > 0)                msg += toString(num_events) + " event(s), ";
		if(num_photos > 0)                msg += toString(num_photos) + " photo(s), ";
		removeSuffixInPlace(msg, ", ");
		msg += " in " + timer.elapsedStringNSigFigs(4);
		conPrint(msg);
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


std::string ServerAllWorldsState::getCredential(const std::string& key) // Throws glare::Exception if not found
{
	Lock lock(mutex);

	auto res = server_credentials.creds.find(key);
	if(res == server_credentials.creds.end())
		throw glare::Exception("Couldn't find '" + key + "' in credentials.");
	return res->second;
}


UID ServerAllWorldsState::getNextObjectUID()
{
	Lock lock(mutex);

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


uint64 ServerAllWorldsState::getNextNewsPostUID()
{
	Lock lock(mutex);

	uint64 highest_id = 0;

	for(auto it = news_posts.begin(); it != news_posts.end(); ++it)
		highest_id = myMax(highest_id, it->first);

	return highest_id + 1;
}


uint64 ServerAllWorldsState::getNextEventUID()
{
	Lock lock(mutex);

	uint64 highest_id = 0;

	for(auto it = events.begin(); it != events.end(); ++it)
		highest_id = myMax(highest_id, it->first);

	return highest_id + 1;
}


uint64 ServerAllWorldsState::getNextPhotoUID()
{
	Lock lock(mutex);

	uint64 highest_id = 0;

	for(auto it = photos.begin(); it != photos.end(); ++it)
		highest_id = myMax(highest_id, it->first);

	return highest_id + 1;
}


ObjectStorageItemRef ServerAllWorldsState::getOrCreateObjectStorageItem(const ObjectStorageKey& key)
{
	auto res = object_storage_items.find(key);
	if(res == object_storage_items.end())
	{
		const uint64 MAX_NUM_STORAGE_ITEMS_PER_OB = 1000;
		if(object_num_storage_items[key.ob_uid] >= MAX_NUM_STORAGE_ITEMS_PER_OB)
			throw glare::Exception("Too many object storage items for object");

		ObjectStorageItemRef new_item = new ObjectStorageItem();
		new_item->key = key;
		object_storage_items.insert(std::make_pair(key, new_item));

		object_num_storage_items[key.ob_uid]++;

		return new_item;
	}
	else
		return res->second;
}


void ServerAllWorldsState::clearObjectStorageItems()
{
	object_storage_items.clear();
	object_num_storage_items.clear();
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


void ServerAllWorldsState::setWorldState(const std::string& world_name, Reference<ServerWorldState> world)
{
	world_states[world_name] = world;
	if(world_name.empty())
		root_world_state = world;
}


FeatureFlagInfo::FeatureFlagInfo()
:	feature_flags(
		ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG | // Enable scripts by default
		ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG | 
		ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG
	),
	db_dirty(false) 
{}
