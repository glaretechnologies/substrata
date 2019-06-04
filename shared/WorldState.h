/*=====================================================================
WorldState.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include <ThreadSafeRefCounted.h>
#include <map>
#include <Mutex.h>


/*=====================================================================
WorldState
-------------------

=====================================================================*/
class WorldState : public ThreadSafeRefCounted
{
public:
	WorldState();
	~WorldState();


	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, Reference<WorldObject>> objects;

	std::set<Reference<WorldObject> > instances; // Objects created by the instancing command in scripts.

	std::map<ParcelID, ParcelRef> parcels;

	Mutex mutex;

	// Just used on clients:
	void updateWithGlobalTimeReceived(double t);
	double getCurrentGlobalTime() const;

	double last_global_time_received;
	double local_time_global_time_received;

	double correction_start_time; // Time we started correcting/skewing to the target time, as measured with Clock::getCurTimeRealSec().
	double correction_amount; // Clock delta.  At the end of the correction time we want to have changed the current time by this much.
private:

};
