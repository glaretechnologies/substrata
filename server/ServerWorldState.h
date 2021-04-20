/*=====================================================================
ServerWorldState.h
-------------------
Copyright Glare Technologies Limited 2018 -
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
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <map>
#include <unordered_set>


class ServerWorldState : public ThreadSafeRefCounted
{
public:

	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, WorldObjectRef> objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_remote_objects;

	std::map<ParcelID, ParcelRef> parcels;
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
	void serialiseToDisk(const std::string& path);
	void denormaliseData(); // Build/update cached/denormalised fields like creator_name.  Mutex should be locked already.

	void updateFromDatabase();
	
	UID getNextObjectUID(); // Gets and then increments next_object_uid
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.
	uint64 getNextOrderUID(); // Gets and then increments next_order_uid.  Locks mutex.

	void markAsChanged() { changed = 1; }
	void clearChangedFlag() { changed = 0; }
	bool hasChanged() const { return changed != 0; }

	Reference<ServerWorldState> getRootWorldState() { return world_states[""]; } // Guaranteed to be return a non-null reference

	Reference<ResourceManager> resource_manager;

	std::map<UserID, Reference<User>> user_id_to_users;  // User id to user
	std::map<std::string, Reference<User>> name_to_users; // Username to user

	std::map<uint64, OrderRef> orders; // Order ID to order

	std::map<std::string, Reference<ServerWorldState> > world_states;

	std::map<std::string, UserWebSessionRef> user_web_sessions; // Map from key to UserWebSession
	
	std::map<uint32, ParcelAuctionRef> parcel_auctions; // ParcelAuction id to ParcelAuction

	std::map<uint64, ScreenshotRef> screenshots;// Screenshot id to ScreenshotRef

	// Ephemeral state that is not serialised to disk.  Set by CoinbasePollerThread.
	double BTC_per_EUR;
	double ETH_per_EUR;

	::Mutex mutex;
private:
	GLARE_DISABLE_COPY(ServerAllWorldsState);

	glare::AtomicInt changed;

	UID next_object_uid;
	UID next_avatar_uid;
	uint64 next_order_uid;
};
