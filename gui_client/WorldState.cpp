/*=====================================================================
WorldState.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#include "WorldState.h"


#include <ConPrint.h>
#include <StringUtils.h>
#include <Clock.h>


WorldState::WorldState()
:	last_global_time_received(0),
	local_time_global_time_received(0),
	correction_start_time(0),
	correction_amount(0)
{
}


WorldState::~WorldState()
{

}


static const double CORRECTION_PERIOD = 2.0;


void WorldState::updateWithGlobalTimeReceived(double t)
{
	const double clock_cur_time = Clock::getCurTimeRealSec();

	if(local_time_global_time_received == 0)
	{
		this->last_global_time_received = t;
		this->local_time_global_time_received = clock_cur_time;
	}
	else
	{
		// 'apply' current correction (offset last_global_time_received by the current correction)
		const double time_since_correction_start = clock_cur_time - this->correction_start_time;
		const double correction_factor = myClamp(time_since_correction_start / CORRECTION_PERIOD, 0.0, 1.0);
		const double correction = correction_factor * this->correction_amount;
		this->last_global_time_received += correction;

		// Compute estimated global time, with no correction (since it has just been applied)
		const double time_since_last_rcv = clock_cur_time - this->local_time_global_time_received;
		const double current_estimated_global_time = this->last_global_time_received + time_since_last_rcv;
		const double diff = t - current_estimated_global_time; // Target global time (as sent from server) minus our local estimate of the global time.

		// Start a new correction period, which when completed will bring the times in sync.
		this->correction_amount = diff;
		this->correction_start_time = clock_cur_time;
	}
}


double WorldState::getCurrentGlobalTime() const
{
	const double clock_cur_time = Clock::getCurTimeRealSec();

	const double time_since_last_rcv = clock_cur_time - this->local_time_global_time_received;
	assert(time_since_last_rcv >= 0);

	// The correction linearly ramps from 0 to correction_amount, at CORRECTION_PERIOD seconds after the correction starts.
	const double time_since_correction_start = clock_cur_time - this->correction_start_time;
	const double correction_factor = myClamp(time_since_correction_start / CORRECTION_PERIOD, 0.0, 1.0);
	const double correction = correction_factor * this->correction_amount;

	return this->last_global_time_received + time_since_last_rcv + correction;
}
