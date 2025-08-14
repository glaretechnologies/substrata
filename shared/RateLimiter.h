/*=====================================================================
RateLimiter.h
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <utils/ThreadSafeRefCounted.h>
#include <utils/CircularBuffer.h>


/*=====================================================================
RateLimiter
-----------
Sliding window rate-limiting code
=====================================================================*/
class RateLimiter : public ThreadSafeRefCounted
{
public:
	RateLimiter(double period, size_t max_num_in_period);
	~RateLimiter();

	// Returns true if event allowed
	bool checkAddEvent(double cur_time);

	static void test();

private:
	struct Event
	{
		double time;
	};

	CircularBuffer<Event> events; // Events that are in the last 'period' seconds.
	double period;
	size_t max_num_in_period;
};
