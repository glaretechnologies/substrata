/*=====================================================================
WorldState.h
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include "../shared/GroundPatch.h"
#include <ThreadSafeRefCounted.h>
#include <PoolMap2.h>
#include <Mutex.h>
#include <map>
#include <unordered_set>
class URLWhitelist;


/*=====================================================================
WorldState
----------
Used on the client.
=====================================================================*/
class WorldState : public ThreadSafeRefCounted
{
public:
	WorldState();
	~WorldState();

	// Just used on clients:
	void updateWithGlobalTimeReceived(double t);
	double getCurrentGlobalTime() const;

	size_t getTotalMemUsage() const;

	Parcel* getParcelPointIsIn(const Vec3d& p); // Returns NULL if not in any parcel.  A lock on mutex must be held by the caller.

	std::map<UID, Reference<Avatar>> avatars;

	glare::PoolMap2<UID, WorldObjectRef, UIDHasher> objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_remote_objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_local_objects;

	std::map<ParcelID, ParcelRef> parcels;
	std::unordered_set<ParcelRef, ParcelRefHash> dirty_from_remote_parcels;
	std::unordered_set<ParcelRef, ParcelRefHash> dirty_from_local_parcels;

	std::map<GroundPatchUID, GroundPatchRef> ground_patches;

	mutable Mutex mutex;

	

	double last_global_time_received;
	double local_time_global_time_received;

	double correction_start_time; // Time we started correcting/skewing to the target time, as measured with Clock::getCurTimeRealSec().
	double correction_amount; // Clock delta.  At the end of the correction time we want to have changed the current time by this much.

	URLWhitelist* url_whitelist; // Pointer to reduce include parse time.
private:

};
