/*=====================================================================
ServerWorldState.h
------------------
Copyright Glare Technologies Limited 2021 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include "User.h"
#include "Order.h"
#include "UserWebSession.h"
#include "ParcelAuction.h"
#include "Screenshot.h"
#include "SubEthTransaction.h"
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <Database.h>
#include <map>
#include <unordered_set>


class ServerWorldState : public ThreadSafeRefCounted
{
public:

	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, WorldObjectRef> objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_remote_objects;

	std::unordered_set<ParcelRef, ParcelRefHash> db_dirty_parcels;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> db_dirty_world_objects;

	void addParcelAsDBDirty(const ParcelRef parcel) { db_dirty_parcels.insert(parcel); }
	void addWorldObjectAsDBDirty(const WorldObjectRef ob) { db_dirty_world_objects.insert(ob); }

	std::map<ParcelID, ParcelRef> parcels;
};


struct OpenSeaParcelListing
{
	ParcelID parcel_id;
};


struct TileInfo
{
	ScreenshotRef cur_tile_screenshot;
	ScreenshotRef prev_tile_screenshot;
};


struct LastParcelUpdateInfo
{
	LastParcelUpdateInfo() : db_dirty(false) {}

	int last_parcel_sale_update_hour;
	int last_parcel_sale_update_day;
	int last_parcel_sale_update_year;

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct EthInfo
{
	EthInfo() : db_dirty(false) {}

	int min_next_nonce;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct MapTileInfo
{
	MapTileInfo() : db_dirty(false) {}

	std::map<Vec3<int>, TileInfo> info;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct ServerCredentials
{
	std::map<std::string, std::string> creds;
};


/*=====================================================================
ServerWorldState
----------------

=====================================================================*/
class ServerAllWorldsState : public ThreadSafeRefCounted
{
public:
	ServerAllWorldsState();
	~ServerAllWorldsState();

	void readFromDisk(const std::string& path);
	void createNewDatabase(const std::string& path);
	void serialiseToDisk(const std::string& path); // Write any changed data (objects in dirty set) to disk.
	void denormaliseData(); // Build/update cached/denormalised fields like creator_name.  Mutex should be locked already.

	UID getNextObjectUID(); // Gets and then increments next_object_uid
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.
	uint64 getNextOrderUID(); // Gets and then increments next_order_uid.  Locks mutex.
	uint64 getNextSubEthTransactionUID();
	uint64 getNextScreenshotUID();

	void markAsChanged() { changed = 1; }
	void clearChangedFlag() { changed = 0; }
	bool hasChanged() const { return changed != 0; }

	void setUserWebMessage(const UserID& user_id, const std::string& s);
	std::string getAndRemoveUserWebMessage(const UserID& user_id); // returns empty string if no message or user

	Reference<ServerWorldState> getRootWorldState() { return world_states[""]; } // Guaranteed to return a non-null reference

	void addResourcesAsDBDirty(const ResourceRef resource) { db_dirty_resources.insert(resource); changed = 1; }
	void addSubEthTransactionAsDBDirty(const SubEthTransactionRef trans) { db_dirty_sub_eth_transactions.insert(trans); changed = 1; }
	void addOrderAsDBDirty(const OrderRef order) { db_dirty_orders.insert(order); changed = 1; }
	void addParcelAuctionAsDBDirty(const ParcelAuctionRef parcel_auction) { db_dirty_parcel_auctions.insert(parcel_auction); changed = 1; }
	void addUserWebSessionAsDBDirty(const UserWebSessionRef screenshot) { db_dirty_userwebsessions.insert(screenshot); changed = 1; }
	void addScreenshotAsDBDirty(const ScreenshotRef screenshot) { db_dirty_screenshots.insert(screenshot); changed = 1; }
	void addUserAsDBDirty(const UserRef user) { db_dirty_users.insert(user); changed = 1; }

	void addEverythingToDirtySets();
	
	Reference<ResourceManager> resource_manager;

	std::map<UserID, Reference<User>> user_id_to_users;  // User id to user
	std::map<std::string, Reference<User>> name_to_users; // Username to user

	std::map<uint64, OrderRef> orders; // Order ID to order

	std::map<std::string, Reference<ServerWorldState> > world_states; // ServerWorldState contains WorldObjects and Parcels

	std::map<std::string, UserWebSessionRef> user_web_sessions; // Map from key to UserWebSession
	
	std::map<uint32, ParcelAuctionRef> parcel_auctions; // ParcelAuction id to ParcelAuction

	std::map<uint64, ScreenshotRef> screenshots;// Screenshot id to ScreenshotRef

	std::map<uint64, SubEthTransactionRef> sub_eth_transactions; // SubEthTransaction id to SubEthTransaction


	// For the map:
	MapTileInfo map_tile_info;

	LastParcelUpdateInfo last_parcel_update_info;

	EthInfo eth_info;

	// Ephemeral state that is not serialised to disk.  Set by CoinbasePollerThread.
	double BTC_per_EUR;
	double ETH_per_EUR;

	// Ephemeral state that is not serialised to disk.  Set by OpenSeaPollerThread.
	std::vector<OpenSeaParcelListing> opensea_parcel_listings;

	// Ephemeral state
	TimeStamp last_screenshot_bot_contact_time;
	TimeStamp last_lightmapper_bot_contact_time;
	TimeStamp last_eth_bot_contact_time;

	std::map<UserID, std::string> user_web_messages; // For displaying an informational or error message on the next webpage served to a user.

	// Sets of objects that should be written to (updated) in the database.
	std::unordered_set<ResourceRef, ResourceRefHash> db_dirty_resources;
	std::unordered_set<SubEthTransactionRef, SubEthTransactionRefHash> db_dirty_sub_eth_transactions;
	std::unordered_set<OrderRef, OrderRefHash> db_dirty_orders;
	std::unordered_set<ParcelAuctionRef, ParcelAuctionRefHash> db_dirty_parcel_auctions;
	std::unordered_set<UserWebSessionRef, UserWebSessionRefHash> db_dirty_userwebsessions;
	std::unordered_set<ScreenshotRef, ScreenshotRefHash> db_dirty_screenshots;
	std::unordered_set<UserRef, UserRefHash> db_dirty_users;

	std::unordered_set<DatabaseKey, DatabaseKeyHash> db_records_to_delete;


	ServerCredentials server_credentials;

	::Mutex mutex;
private:
	GLARE_DISABLE_COPY(ServerAllWorldsState);

	glare::AtomicInt changed;

	UID next_object_uid;
	UID next_avatar_uid;
	uint64 next_order_uid;
	uint64 next_sub_eth_transaction_uid;

	Database database;
};
