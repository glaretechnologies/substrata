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
#include "../shared/WorldStateLock.h"
#include "../shared/LODChunk.h"
#include <ThreadSafeRefCounted.h>
#include <FastIterMap.h>
#include <FastIterMap.h>
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

	Parcel* getParcelPointIsIn(const Vec3d& p, ParcelID guess_parcel_id = ParcelID::invalidParcelID()) REQUIRES(mutex); // Returns NULL if not in any parcel.  A lock on mutex must be held by the caller.

	std::map<UID, Reference<Avatar>> avatars GUARDED_BY(mutex);

	glare::AtomicInt avatars_changed;


	glare::FastIterMap<UID, WorldObjectRef, UIDHasher> objects GUARDED_BY(mutex);
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_remote_objects GUARDED_BY(mutex);
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_local_objects GUARDED_BY(mutex);

	std::map<ParcelID, ParcelRef> parcels GUARDED_BY(mutex);
	std::unordered_set<ParcelRef, ParcelRefHash> dirty_from_remote_parcels GUARDED_BY(mutex);
	std::unordered_set<ParcelRef, ParcelRefHash> dirty_from_local_parcels GUARDED_BY(mutex);

	std::map<Vec3i, LODChunkRef> lod_chunks GUARDED_BY(mutex);
	std::unordered_set<LODChunkRef, LODChunkRefHash> dirty_from_remote_lod_chunks GUARDED_BY(mutex);


	// For each object uid: event handlers added in a script for an object that was not yet created.
	std::map<UID, Reference<ObjectEventHandlers> > pending_event_handlers; 

	mutable WorldStateMutex mutex;


	std::map<GroundPatchUID, GroundPatchRef> ground_patches;

	URLWhitelist* url_whitelist; // Pointer to reduce include parse time.
private:
	double last_global_time_received GUARDED_BY(mutex);
	double local_time_global_time_received GUARDED_BY(mutex);

	double correction_start_time GUARDED_BY(mutex); // Time we started correcting/skewing to the target time, as measured with Clock::getCurTimeRealSec().
	double correction_amount GUARDED_BY(mutex); // Clock delta.  At the end of the correction time we want to have changed the current time by this much.
};
