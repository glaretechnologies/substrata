/*=====================================================================
RateLimiter.cpp
---------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "RateLimiter.h"


RateLimiter::RateLimiter(double period_, size_t max_num_in_period_)
:	period(period_), max_num_in_period(max_num_in_period_)
{}


RateLimiter::~RateLimiter()
{}


bool RateLimiter::checkAddEvent(double cur_time)
{
	// Remove any events that are now out of the period
	const double start_time = cur_time - period;

	while(events.nonEmpty() && (events.front().time < start_time))
	{
		events.pop_front();
	}

	// Now see if we can add the new event
	if(events.size() < max_num_in_period)
	{
		events.push_back(Event({cur_time}));
		return true;
	}
	else
		return false;
}


#if BUILD_TESTS


#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"
#include "../utils/TestUtils.h"
#include "../maths/PCG32.h"
#include <Timer.h>


void RateLimiter::test()
{
	conPrint("RateLimiter::test()");

	{
		RateLimiter r(/*period=*/1.0, /*max_num_in_period=*/4);

		testAssert(r.checkAddEvent(0.0) == true);
		testAssert(r.checkAddEvent(0.1) == true);
		testAssert(r.checkAddEvent(0.2) == true);
		testAssert(r.checkAddEvent(0.3) == true);

		testAssert(r.checkAddEvent(0.4) == false);
		testAssert(r.checkAddEvent(0.4) == false);
		testAssert(r.checkAddEvent(0.4) == false);
		testAssert(r.checkAddEvent(0.4) == false);

		testAssert(r.checkAddEvent(0.99) == false);
		testAssert(r.checkAddEvent(1.01) == true); // This should remove event at 0.0.  New start time is 0.1.

		testAssert(r.checkAddEvent(1.0999) == false);
		testAssert(r.checkAddEvent(1.1001) == true); // This should remove event at 0.1
	}

	conPrint("RateLimiter::test() done");
}


#endif // BUILD_TESTS
