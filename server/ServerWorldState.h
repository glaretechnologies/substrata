/*=====================================================================
ServerWorldState.h
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include "../shared/WorldSettings.h"
#include "../shared/WorldDetails.h"
#include "../shared/WorldStateLock.h"
#include "../shared/LODChunk.h"
#include "../shared/SubstrataLuaVM.h"
#include "NewsPost.h"
#include "SubEvent.h"
#include "User.h"
#include "Order.h"
#include "UserWebSession.h"
#include "ParcelAuction.h"
#include "Screenshot.h"
#include "Photo.h"
#include "SubEthTransaction.h"
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <Database.h>
#include <CircularBuffer.h>
#include <HashMap.h>
#include <map>
#include <unordered_set>
class ServerWorldState;
class WebDataStore;


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


struct FeatureFlagInfo
{
	FeatureFlagInfo();

	uint64 feature_flags; // A combination of ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG etc.

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct MigrationVersionInfo
{
	MigrationVersionInfo() : db_dirty(false) {}

	uint32 migration_version;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct ServerCredentials
{
	std::map<std::string, std::string> creds;
};


struct UserScriptLogMessage
{
	TimeStamp time;

	enum MessageType
	{
		MessageType_print,
		MessageType_error
	};
	MessageType msg_type;

	UID script_ob_uid;
	std::string msg;
};

struct UserScriptLog : public ThreadSafeRefCounted
{
	CircularBuffer<UserScriptLogMessage> messages GUARDED_BY(mutex);
	Mutex mutex;
};


struct ObjectStorageKey
{
	ObjectStorageKey() {}
	ObjectStorageKey(const UID& ob_uid_, const std::string& key_string_) : ob_uid(ob_uid_), key_string(key_string_) {}

	UID ob_uid;
	std::string key_string;

	inline bool operator == (const ObjectStorageKey& other) const { return ob_uid == other.ob_uid && key_string == other.key_string; }
	inline bool operator < (const ObjectStorageKey& other) const
	{
		if(ob_uid < other.ob_uid)
			return true;
		else if(ob_uid > other.ob_uid)
			return false;
		else
			return key_string < other.key_string;
	}
};


struct ObjectStorageItem : public ThreadSafeRefCounted
{
	ObjectStorageKey key;
	glare::AllocatorVector<unsigned char, 16> data;
	DatabaseKey database_key;
};
typedef Reference<ObjectStorageItem> ObjectStorageItemRef;
	
struct ObjectStorageItemRefHash
{
	size_t operator() (const ObjectStorageItemRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


struct UserSecretKey
{
	UserSecretKey() {}
	UserSecretKey(const UserID& user_id_, const std::string& secret_name_) : user_id(user_id_), secret_name(secret_name_) {}

	UserID user_id;
	std::string secret_name;

	inline bool operator == (const UserSecretKey& other) const { return user_id == other.user_id && secret_name == other.secret_name; }
	inline bool operator < (const UserSecretKey& other) const
	{
		if(user_id < other.user_id)
			return true;
		else if(user_id > other.user_id)
			return false;
		else
			return secret_name < other.secret_name;
	}
};


struct UserSecret : public ThreadSafeRefCounted
{
	UserSecretKey key;
	std::string value;
	DatabaseKey database_key;

	static const size_t MAX_SECRET_NAME_SIZE            = 1000;
	static const size_t MAX_VALUE_SIZE                  = 10000;
};
typedef Reference<UserSecret> UserSecretRef;
	
struct UserSecretRefHash
{
	size_t operator() (const UserSecretRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


/*=====================================================================
ServerWorldState
----------------
State for a particular world.

Due to limitations of the Thread Safety Analysis, which doesn't seem to handle
references in maps, we will enforce that the using thread holds the world state mutex by making
members private and having accessor methods that take a WorldStateLock argument.
=====================================================================*/
class ServerWorldState : public ThreadSafeRefCounted
{
public:
	ServerWorldState() : db_dirty(false) {}

	void addParcelAsDBDirty     (const ParcelRef parcel,  WorldStateLock& /*world_state_lock*/) { db_dirty_parcels.insert(parcel); }
	void addWorldObjectAsDBDirty(const WorldObjectRef ob, WorldStateLock& /*world_state_lock*/) { db_dirty_world_objects.insert(ob); }
	void addLODChunkAsDBDirty   (const LODChunkRef ob,    WorldStateLock& /*world_state_lock*/) { db_dirty_lod_chunks.insert(ob); }

	void writeToStream(RandomAccessOutStream& stream) const;

	WorldSettings world_settings;

	WorldDetails details; // owner ID, name, description etc.
	
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.


	typedef std::map<UID, Reference<Avatar>> AvatarMapType;
	typedef std::map<UID, WorldObjectRef> ObjectMapType;
	typedef std::map<ParcelID, ParcelRef> ParcelMapType;
	typedef std::map<Vec3i, LODChunkRef> LODChunkMapType;
	typedef std::unordered_set<WorldObjectRef, WorldObjectRefHash> DirtyFromRemoteObjectSetType;

	AvatarMapType&     getAvatars(WorldStateLock& /*world_state_lock*/) { return avatars; }
	ObjectMapType&     getObjects(WorldStateLock& /*world_state_lock*/) { return objects; }
	ParcelMapType&     getParcels(WorldStateLock& /*world_state_lock*/) { return parcels; }
	LODChunkMapType& getLODChunks(WorldStateLock& /*world_state_lock*/) { return lod_chunks; }
	
	ParcelMapType parcels; // TODO: make private.  Lots of compile errors to fix when doing so.

	DirtyFromRemoteObjectSetType&                           getDirtyFromRemoteObjects(WorldStateLock& /*world_state_lock*/) { return dirty_from_remote_objects; }
	std::unordered_set<WorldObjectRef, WorldObjectRefHash>& getDBDirtyWorldObjects(WorldStateLock& /*world_state_lock*/) { return db_dirty_world_objects; }
	std::unordered_set<ParcelRef, ParcelRefHash>&           getDBDirtyParcels(WorldStateLock& /*world_state_lock*/) { return db_dirty_parcels; }
	std::unordered_set<LODChunkRef, LODChunkRefHash>&       getDBDirtyLODChunks(WorldStateLock& /*world_state_lock*/) { return db_dirty_lod_chunks; }

private:
	ObjectMapType objects;
	DirtyFromRemoteObjectSetType dirty_from_remote_objects; // TODO: could just use vector for this, and avoid duplicates by checking object dirty flag.
	AvatarMapType avatars;
	LODChunkMapType lod_chunks;

	std::unordered_set<WorldObjectRef, WorldObjectRefHash>	db_dirty_world_objects;
	std::unordered_set<ParcelRef, ParcelRefHash>			db_dirty_parcels;
	std::unordered_set<LODChunkRef, LODChunkRefHash>		db_dirty_lod_chunks;
};

typedef Reference<ServerWorldState> ServerWorldStateRef;

struct ServerWorldStateRefHash
{
	size_t operator() (const ServerWorldStateRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};

void readServerWorldStateFromStream(RandomAccessInStream& stream, ServerWorldState& world);


/*=====================================================================
ServerAllWorldsState
--------------------

=====================================================================*/
class ServerAllWorldsState : public ThreadSafeRefCounted
{
public:
	ServerAllWorldsState();
	~ServerAllWorldsState();

	void readFromDisk(const std::string& path);
	void createNewDatabase(const std::string& path);
	void serialiseToDisk(WorldStateLock& lock) REQUIRES(mutex); // Write any changed data (objects in dirty set) to disk.  Mutex should be held already.
	void denormaliseData(); // Build/update cached/denormalised fields like creator_name.
	void doMigrations(WorldStateLock& lock) REQUIRES(mutex);

	// Removes sensitive information from the database, such as user passwords, email addresses, billing information, web sessions etc.
	// Then saves the updates to disk.
	void saveSanitisedDatabase();

	std::string getCredential(const std::string& key); // Throws glare::Exception if not found

	UID getNextObjectUID(); // Gets and then increments next_object_uid.  Locks mutex.
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.
	uint64 getNextOrderUID(); // Gets and then increments next_order_uid.  Locks mutex.
	uint64 getNextSubEthTransactionUID();
	uint64 getNextScreenshotUID();
	uint64 getNextNewsPostUID();
	uint64 getNextEventUID();
	uint64 getNextPhotoUID();

	ObjectStorageItemRef getOrCreateObjectStorageItem(const ObjectStorageKey& key) REQUIRES(mutex); // Throws exception if too many items for object already created.
	void clearObjectStorageItems() REQUIRES(mutex); // Just for tests

	void markAsChanged() { changed = 1; }
	void clearChangedFlag() { changed = 0; }
	bool hasChanged() const { return changed != 0; }

	void setUserWebMessage(const UserID& user_id, const std::string& s);
	std::string getAndRemoveUserWebMessage(const UserID& user_id); // returns empty string if no message or user

	inline Reference<ServerWorldState> getRootWorldState(); // Guaranteed to return a non-null reference


	void addResourceAsDBDirty(const ResourceRef resource)					REQUIRES(mutex) { db_dirty_resources.insert(resource); changed = 1; }
	void addSubEthTransactionAsDBDirty(const SubEthTransactionRef trans)	REQUIRES(mutex) { db_dirty_sub_eth_transactions.insert(trans); changed = 1; }
	void addOrderAsDBDirty(const OrderRef order)							REQUIRES(mutex) { db_dirty_orders.insert(order); changed = 1; }
	void addParcelAuctionAsDBDirty(const ParcelAuctionRef parcel_auction)	REQUIRES(mutex) { db_dirty_parcel_auctions.insert(parcel_auction); changed = 1; }
	void addUserWebSessionAsDBDirty(const UserWebSessionRef screenshot)		REQUIRES(mutex) { db_dirty_userwebsessions.insert(screenshot); changed = 1; }
	void addScreenshotAsDBDirty(const ScreenshotRef screenshot)				REQUIRES(mutex) { db_dirty_screenshots.insert(screenshot); changed = 1; }
	void addPhotoAsDBDirty(const PhotoRef photo)							REQUIRES(mutex) { db_dirty_photos.insert(photo); changed = 1; }
	void addUserAsDBDirty(const UserRef user)								REQUIRES(mutex) { db_dirty_users.insert(user); changed = 1; }
	void addNewsPostAsDBDirty(const NewsPostRef post)						REQUIRES(mutex) { db_dirty_news_posts.insert(post); changed = 1; }
	void addEventAsDBDirty(const SubEventRef event)							REQUIRES(mutex) { db_dirty_events.insert(event); changed = 1; }

	void addEverythingToDirtySets();

	bool isInReadOnlyMode();

	void clearAndReset(); // Just for fuzzing

	void addPersonalWorldForUser(const UserRef user, WorldStateLock& lock) REQUIRES(mutex);

	Reference<ResourceManager> resource_manager;

	std::map<UserID, Reference<User>> user_id_to_users GUARDED_BY(mutex);  // User id to user
	std::map<std::string, Reference<User>> name_to_users GUARDED_BY(mutex); // Username to user

	std::map<uint64, OrderRef> orders GUARDED_BY(mutex); // Order ID to order

	std::map<std::string, Reference<ServerWorldState> > world_states GUARDED_BY(mutex); // ServerWorldState contains WorldObjects and Parcels
	Reference<ServerWorldState> root_world_state GUARDED_BY(mutex); // = world_states[""]

	std::map<std::string, UserWebSessionRef> user_web_sessions GUARDED_BY(mutex); // Map from key to UserWebSession
	
	std::map<uint32, ParcelAuctionRef> parcel_auctions GUARDED_BY(mutex); // ParcelAuction id to ParcelAuction

	std::map<uint64, ScreenshotRef> screenshots GUARDED_BY(mutex); // Screenshot id to ScreenshotRef

	std::map<uint64, PhotoRef> photos GUARDED_BY(mutex); // Photo id to PhotoRef

	std::map<uint64, SubEthTransactionRef> sub_eth_transactions GUARDED_BY(mutex); // SubEthTransaction id to SubEthTransaction

	std::map<uint64, NewsPostRef> news_posts GUARDED_BY(mutex); // NewsPost id to NewsPost

	std::map<ObjectStorageKey, ObjectStorageItemRef> object_storage_items GUARDED_BY(mutex);
	std::unordered_map<UID, uint64, UIDHasher> object_num_storage_items GUARDED_BY(mutex); // Map from object UID to number of storage items for object.

	std::map<UserSecretKey, UserSecretRef> user_secrets GUARDED_BY(mutex);

	std::map<uint64, SubEventRef> events GUARDED_BY(mutex); // SubEvent id to SubEvent

	HashMap<UserID, Reference<SubstrataLuaVM>, UserIDHasher> lua_vms;

	// For the map:
	MapTileInfo map_tile_info;

	LastParcelUpdateInfo last_parcel_update_info;

	EthInfo eth_info;

	MigrationVersionInfo migration_version_info;

	static const uint64 SERVER_SCRIPT_EXEC_FEATURE_FLAG                   = 1; // Is server-side script execution enabled?
	static const uint64 LUA_HTTP_REQUESTS_FEATURE_FLAG                    = 2; // Are Lua-initiated HTTP requests enabled?
	static const uint64 DO_WORLD_MAINTENANCE_FEATURE_FLAG                 = 4; // Should world maintenance tasks run? (e.g. WorldMaintenance::removeOldVehicles())
	FeatureFlagInfo feature_flag_info GUARDED_BY(mutex);


	// Ephemeral state that is not serialised to disk.  Set by CoinbasePollerThread.
	double BTC_per_EUR;
	double ETH_per_EUR;

	// Ephemeral state that is not serialised to disk.  Set by OpenSeaPollerThread.
	std::vector<OpenSeaParcelListing> opensea_parcel_listings GUARDED_BY(mutex);

	// Ephemeral state
	TimeStamp last_screenshot_bot_contact_time GUARDED_BY(mutex);
	TimeStamp last_lightmapper_bot_contact_time GUARDED_BY(mutex);
	TimeStamp last_eth_bot_contact_time GUARDED_BY(mutex);

	// Ephemeral state - a message that is shown to clients
	std::string server_admin_message GUARDED_BY(mutex);
	bool server_admin_message_changed GUARDED_BY(mutex);

	// Ephemeral state - is the server in read-only mode?  When true, clients can't make changes to objects etc.
	bool read_only_mode GUARDED_BY(mutex);

	// Ephemeral state - do we want to force the DynamicTextureUpdaterThread to do a run?
	bool force_dyn_tex_update GUARDED_BY(mutex);

	// Ephemeral state:
	std::map<UserID, Reference<UserScriptLog> > user_script_log GUARDED_BY(mutex);

	std::map<UserID, std::string> user_web_messages GUARDED_BY(mutex); // For displaying an informational or error message on the next webpage served to a user.

	// Sets of objects that should be written to (updated) in the database.
	std::unordered_set<ResourceRef, ResourceRefHash>					db_dirty_resources				GUARDED_BY(mutex);
	std::unordered_set<SubEthTransactionRef, SubEthTransactionRefHash>	db_dirty_sub_eth_transactions	GUARDED_BY(mutex);
	std::unordered_set<NewsPostRef, NewsPostRefHash>					db_dirty_news_posts				GUARDED_BY(mutex);
	std::unordered_set<OrderRef, OrderRefHash>							db_dirty_orders					GUARDED_BY(mutex);
	std::unordered_set<ParcelAuctionRef, ParcelAuctionRefHash>			db_dirty_parcel_auctions		GUARDED_BY(mutex);
	std::unordered_set<UserWebSessionRef, UserWebSessionRefHash>		db_dirty_userwebsessions		GUARDED_BY(mutex);
	std::unordered_set<ScreenshotRef, ScreenshotRefHash>				db_dirty_screenshots			GUARDED_BY(mutex);
	std::unordered_set<PhotoRef, PhotoRefHash>							db_dirty_photos					GUARDED_BY(mutex);
	std::unordered_set<UserRef, UserRefHash>							db_dirty_users					GUARDED_BY(mutex);
	std::unordered_set<ObjectStorageItemRef, ObjectStorageItemRefHash>	db_dirty_object_storage_items	GUARDED_BY(mutex);
	std::unordered_set<UserSecretRef, UserSecretRefHash>				db_dirty_user_secrets			GUARDED_BY(mutex);
	std::unordered_set<SubEventRef, SubEventRefHash>					db_dirty_events					GUARDED_BY(mutex);

	std::unordered_set<DatabaseKey, DatabaseKeyHash>					db_records_to_delete			GUARDED_BY(mutex);

	WebDataStore* web_data_store; // Since we pass around ServerAllWorldsState for all the web request handlers, just store a pointer to web_data_store so we can access it.

	ServerCredentials server_credentials;

	mutable ::WorldStateMutex mutex;
private:
	void setWorldState(const std::string& world_name, Reference<ServerWorldState> world);

	GLARE_DISABLE_COPY(ServerAllWorldsState);

	glare::AtomicInt changed;

	UID next_object_uid GUARDED_BY(mutex);
	UID next_avatar_uid GUARDED_BY(mutex);
	uint64 next_order_uid GUARDED_BY(mutex);
	uint64 next_sub_eth_transaction_uid GUARDED_BY(mutex);

	Database database GUARDED_BY(mutex);
};


Reference<ServerWorldState> ServerAllWorldsState::getRootWorldState() 
{
#ifndef NDEBUG
	{
		Lock lock(mutex);
		assert(world_states[""] == root_world_state);
	}
#endif
	return root_world_state;
}
