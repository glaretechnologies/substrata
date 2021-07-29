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


ServerAllWorldsState::ServerAllWorldsState()
{
	next_avatar_uid = UID(0);
	next_object_uid = UID(0);
	next_order_uid = 0;
	next_sub_eth_transaction_uid = 0;

	world_states[""] = new ServerWorldState();

	BTC_per_EUR = 0;
	ETH_per_EUR = 0;
}


ServerAllWorldsState::~ServerAllWorldsState()
{
}


static const uint32 WORLD_STATE_MAGIC_NUMBER = 487173571;
static const uint32 WORLD_STATE_SERIALISATION_VERSION = 2;
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
static const uint32 EOS_CHUNK = 1000;


void ServerAllWorldsState::readFromDisk(const std::string& path)
{
	conPrint("Reading world state from '" + path + "'...");
	Timer timer;

	FileInStream stream(path);

	// Read magic number
	const uint32 m = stream.readUInt32();
	if(m != WORLD_STATE_MAGIC_NUMBER)
		throw glare::Exception("Invalid magic number " + toString(m) + ", expected " + toString(WORLD_STATE_MAGIC_NUMBER) + ".");

	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_STATE_SERIALISATION_VERSION)
		throw glare::Exception("Unknown version " + toString(v) + ", expected " + toString(WORLD_STATE_SERIALISATION_VERSION) + ".");

	Reference<ServerWorldState> current_world = new ServerWorldState();
	world_states[""] = current_world;

	size_t num_obs = 0;
	size_t num_parcels = 0;
	size_t num_orders = 0;
	size_t num_sessions = 0;
	size_t num_auctions = 0;
	size_t num_screenshots = 0;
	size_t num_sub_eth_transactions = 0;
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
		else if(chunk == EOS_CHUNK)
		{
			break;
		}
		else
		{
			throw glare::Exception("Unknown chunk type '" + toString(chunk) + "'");
		}
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



	conPrint("Loaded " + toString(num_obs) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " +
		toString(num_parcels) + " parcel(s), " + toString(resource_manager->getResourcesForURL().size()) + " resource(s), " + toString(num_orders) + " order(s), " + 
		toString(num_sessions) + " session(s), " + toString(num_auctions) + " auction(s), " + toString(num_screenshots) + " screenshot(s), " + 
		toString(num_sub_eth_transactions) + " sub eth transaction(s) in " + timer.elapsedStringNSigFigs(4));
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


void ServerAllWorldsState::serialiseToDisk(const std::string& path)
{
	conPrint("Saving world state to disk...");
	Timer timer;

	try
	{
		size_t num_obs = 0;
		size_t num_parcels = 0;
		size_t num_orders = 0;
		size_t num_sessions = 0;
		size_t num_auctions = 0;
		size_t num_screenshots = 0;
		size_t num_sub_eth_transactions = 0;

		const std::string temp_path = path + "_temp";
		{
			FileOutStream stream(temp_path);

			// Write magic number
			stream.writeUInt32(WORLD_STATE_MAGIC_NUMBER);

			// Write version
			stream.writeUInt32(WORLD_STATE_SERIALISATION_VERSION);

			// For each world
			for(auto world_it = world_states.begin(); world_it != world_states.end(); ++world_it)
			{
				Reference<ServerWorldState> world_state = world_it->second;

				// Write world chunk
				stream.writeUInt32(WORLD_CHUNK);

				stream.writeStringLengthFirst(world_it->first); // Write world name.

				// Write objects
				{
					for(auto i=world_state->objects.begin(); i != world_state->objects.end(); ++i)
					{
						stream.writeUInt32(WORLD_OBJECT_CHUNK);
						i->second->writeToStream(stream);
						num_obs++;
					}
				}

				// Write parcels
				{
					for(auto i=world_state->parcels.begin(); i != world_state->parcels.end(); ++i)
					{
						stream.writeUInt32(PARCEL_CHUNK);
						writeToStream(*i->second, stream);
						num_parcels++;
					}
				}
			}

			// Write users
			{
				for(auto i=user_id_to_users.begin(); i != user_id_to_users.end(); ++i)
				{
					stream.writeUInt32(USER_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			// Write resource objects
			{
				for(auto i=resource_manager->getResourcesForURL().begin(); i != resource_manager->getResourcesForURL().end(); ++i)
				{
					stream.writeUInt32(RESOURCE_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			// Write orders
			{
				for(auto i=orders.begin(); i != orders.end(); ++i)
				{
					stream.writeUInt32(ORDER_CHUNK);
					writeToStream(*i->second, stream);
					num_orders++;
				}
			}
			
			// Write UserWebSessions
			{
				for(auto i=user_web_sessions.begin(); i != user_web_sessions.end(); ++i)
				{
					stream.writeUInt32(USER_WEB_SESSION_CHUNK);
					writeToStream(*i->second, stream);
					num_sessions++;
				}
			}
			
			// Write ParcelAuctions
			{
				for(auto i=parcel_auctions.begin(); i != parcel_auctions.end(); ++i)
				{
					stream.writeUInt32(PARCEL_AUCTION_CHUNK);
					writeToStream(*i->second, stream);
					num_auctions++;
				}
			}
			
			// Write Screenshots
			{
				for(auto i=screenshots.begin(); i != screenshots.end(); ++i)
				{
					stream.writeUInt32(SCREENSHOT_CHUNK);
					writeToStream(*i->second, stream);
					num_screenshots++;
				}
			}
			
			// Write SubEthTransactions
			{
				for(auto i=sub_eth_transactions.begin(); i != sub_eth_transactions.end(); ++i)
				{
					stream.writeUInt32(SUB_ETH_TRANSACTIONS_CHUNK);
					writeToStream(*i->second, stream);
					num_sub_eth_transactions++;
				}
			}

			stream.writeUInt32(EOS_CHUNK); // Write end-of-stream chunk
		}

		FileUtils::moveFile(temp_path, path);

		conPrint("Saved " + toString(num_obs) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " +
			toString(num_parcels) + " parcel(s), " + toString(resource_manager->getResourcesForURL().size()) + " resource(s), " + toString(num_orders) + " order(s), " + 
			toString(num_sessions) + " session(s), " + toString(num_auctions) + " auction(s), " + toString(num_screenshots) + " screenshot(s), " +
			toString(num_sub_eth_transactions) + " sub eth transction(s) in " + timer.elapsedStringNSigFigs(4));
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
