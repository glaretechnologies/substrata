/*=====================================================================
WorldState.cpp
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WorldState.h"


#include "../shared/ObjectEventHandlers.h"
#include <ConPrint.h>
#include <StringUtils.h>
#include <Clock.h>
#include <Lock.h>
#include <Timer.h>
#include <tracy/Tracy.hpp>


WorldState::WorldState()
:	last_global_time_received(0),
	local_time_global_time_received(0),
	correction_start_time(0),
	correction_amount(0),
	objects(UID::invalidUID()),
	last_ping_send_time(0),
	min_rtt(1.0e10),
	received_rtt(false)
{
}


WorldState::~WorldState()
{
	dirty_from_remote_objects.clear();
	dirty_from_local_objects.clear();

//	for(auto it = objects.begin(); it != objects.end(); ++it)
//	{
//		// Remove implicit reference the world state has to each object.  Do this to avoid hitting assert(refcount == 0); in ~ThreadSafeRefCounted().
//		assert(it.getValuePtr()->getRefCount() == 1);
//		if(it.getValuePtr()->getRefCount() != 1)
//		{
//			WorldObject* ob = it.getValuePtr();
//			conPrint("Warning: ref count of WorldObject about to be destroyed is != 1. ob->uid: " + ob->uid.toString());
//#if defined(_WIN32)
//			__debugbreak();
//#endif
//		}
//		it.getValuePtr()->decRefCount();
//	}
}


void WorldState::clear()
{
	Lock lock(mutex);

	avatars.clear();
	avatars_changed = 1;
	
	objects.clear();
	dirty_from_remote_objects.clear();
	dirty_from_local_objects.clear();

	parcels.clear();
	dirty_from_remote_parcels.clear();
	dirty_from_local_parcels.clear();

	lod_chunks.clear();
	dirty_from_remote_lod_chunks.clear();

	pending_event_handlers.clear();
}


static const double CORRECTION_PERIOD = 2.0;

/*
Case 1: sending of global_time_1 is slower

      global_time_0                global_time_1
-------|------------------------------|------------------------------------------> server
        \                             \
          \                               \
            \                                 \
              \                                   \
---------------|------------------------------------|----------------------------> client 
         local_time_0                        local_time_1

               |<---------------------------------->|
                      local_time_since_last_rcv

-------|-------------------------------------|-----------
                                     cur_estimated_global_time = global_time_0 + local_time_since_last_rcv



Case 2: sending of global_time_1 is faster (lower latency).  This means that global_time_1 leads to the more reliable estimate as we are estimating with the lowest observed latency message.

      global_time_0                global_time_1
-------|------------------------------|------------------------------------------> server
        \                             \
          \                            \
            \                           \
              \                          \
---------------|-------------------------|---------------------------------------> client 
         local_time_0                 local_time_1

               |<----------------------->|
                   local_time_since_last_rcv

-------|--------------------------|-----------
               cur_estimated_global_time = global_time_0 + local_time_since_last_rcv
*/
void WorldState::updateWithGlobalTimeReceived(double t)
{
	Lock lock(mutex);

	const double clock_cur_time = Clock::getCurTimeRealSec();

	if(local_time_global_time_received == 0) // If we have not received a global time yet:
	{
		this->last_global_time_received = t;
		this->local_time_global_time_received = clock_cur_time;
	}
	else
	{
		// See diagram above
		const double local_time_since_last_rcv = clock_cur_time - this->local_time_global_time_received;
		const double cur_estimated_global_time = this->last_global_time_received + local_time_since_last_rcv;

		// conPrint("new global time: " + toString(t) + ", cur_estimated_global_time: " + toString(cur_estimated_global_time));

		if(t > cur_estimated_global_time)
		{
			// conPrint("!!!! Accepting as more accurate global time");

			// Accept it as a more accurate global time (less sending latency)
			this->last_global_time_received = t;
			this->local_time_global_time_received = clock_cur_time;
		}

		// 'apply' current correction (offset last_global_time_received by the current correction) to last_global_time_received
		//const double time_since_correction_start = clock_cur_time - this->correction_start_time;
		//assert(time_since_correction_start >= 0);
		//const double correction_factor = myMin(time_since_correction_start / CORRECTION_PERIOD, 1.0);
		//const double correction = correction_factor * this->correction_amount;
		//this->last_global_time_received += correction;

		//// Compute estimated global time, with no correction (since it has just been applied)
		//const double time_since_last_rcv = clock_cur_time - this->local_time_global_time_received;
		//const double current_estimated_global_time = this->last_global_time_received + time_since_last_rcv;
		//const double diff = t - current_estimated_global_time; // Target global time (as sent from server) minus our local estimate of the global time.

		//// Start a new correction period, which when completed will bring the times in sync.
		//this->correction_amount = diff;
		//this->correction_start_time = clock_cur_time;

		//this->local_time_global_time_received = clock_cur_time;
	}
}


double WorldState::getCurrentGlobalTime() const
{
	Lock lock(mutex);

	const double clock_cur_time = Clock::getCurTimeRealSec();

	const double time_since_last_rcv = clock_cur_time - this->local_time_global_time_received;
	assert(time_since_last_rcv >= 0);

	// The correction linearly ramps from 0 to correction_amount, at CORRECTION_PERIOD seconds after the correction starts.
//	const double time_since_correction_start = clock_cur_time - this->correction_start_time;
//	assert(time_since_correction_start >= 0);
//	const double correction_factor = myMin(time_since_correction_start / CORRECTION_PERIOD, 1.0);
//	const double correction = correction_factor * this->correction_amount;
//
//	const double use_global_time = this->last_global_time_received + time_since_last_rcv + correction;
//
//	const double offset = use_global_time - Clock::getTimeSinceInit();
//	printVar(offset);

	const double estimated_one_way_latency = received_rtt ? (min_rtt * 0.5) : 0.2;

	return this->last_global_time_received + time_since_last_rcv + estimated_one_way_latency;// + correction;
}


void WorldState::newRoundTripTimeComputed(double rtt)
{
	Lock lock(mutex);

	min_rtt = myMin(min_rtt, rtt);

	received_rtt = true;

	// conPrint("newRoundTripTimeComputed(): min_rtt: " + doubleToStringMaxNDecimalPlaces(min_rtt * 1.0e3, 4) + " ms");
}


size_t WorldState::getTotalMemUsage() const
{
	Lock lock(mutex);

	size_t sum = 0;

	for(auto it = objects.valuesBegin(); it != objects.valuesEnd(); ++it)
	{
		sum += it.getValue()->getTotalMemUsage();
	}

	return sum;
}


Parcel* WorldState::getParcelPointIsIn(const Vec3d& p_, ParcelID guess_parcel_id)
{
	ZoneScoped; // Tracy profiler
	//Timer timer;

	const Vec4f p = p_.toVec4fPoint();

	if(guess_parcel_id.valid())
	{
		auto res = parcels.find(guess_parcel_id);
		if(res != parcels.end())
		{
			Parcel* parcel = res->second.ptr();
			if(parcel->aabb.contains(p))
			{
				//conPrint("getParcelPointIsIn using guess_parcel_id took " + timer.elapsedStringMS());
				return parcel;
			}
		}
	}

	for(auto& it : parcels) // NOTE: fixme, crappy linear scan
	{
		Parcel* parcel = it.second.ptr();

		if(parcel->aabb.contains(p))
		{
			//conPrint("getParcelPointIsIn took " + timer.elapsedStringMS());
			return parcel;
		}
	}

	//conPrint("getParcelPointIsIn (finding nothing) took " + timer.elapsedStringMS());
	return NULL;
}
