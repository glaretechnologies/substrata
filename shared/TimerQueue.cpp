/*=====================================================================
TimerQueue.cpp
--------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "TimerQueue.h"


TimerQueueTimer::TimerQueueTimer()
{}


TimerQueueTimer::TimerQueueTimer(double tigger_time_) : tigger_time(tigger_time_)/*, valid(true)*/ {}


TimerQueue::TimerQueue()
{
#if 0
	bucket_max_ttt[0] = 0.03125;
	bucket_max_ttt[1] = 0.125;
	bucket_max_ttt[2] = 0.5;
	bucket_max_ttt[3] = 2;
	bucket_max_ttt[4] = 8;
	bucket_max_ttt[5] = 32;
	bucket_max_ttt[6] = 128;
	bucket_max_ttt[7] = std::numeric_limits<double>::infinity();

	bucket_min_ttt[0] = 0;
	bucket_min_ttt[1] = 0.03125;
	bucket_min_ttt[2] = 0.125;
	bucket_min_ttt[3] = 0.5;
	bucket_min_ttt[4] = 2;
	bucket_min_ttt[5] = 8;
	bucket_min_ttt[6] = 32;
	bucket_min_ttt[7] = 128;

	// Initialise buckets
	for(size_t b=0; b<8; ++b)
	{
		TimerQueueBucket& bucket = buckets[b];

		const size_t initial_num = 1024;
		bucket.timers.resize(initial_num);
		bucket.freelist.resize(initial_num);

		for(size_t i=0; i<initial_num; ++i)
			bucket.timers[i].valid = false;

		for(size_t i=0; i<initial_num; ++i)
			bucket.freelist[i] = (int)i;
	}
#endif
}


TimerQueue::~TimerQueue()
{
}


void TimerQueue::addTimer(double cur_time, const TimerQueueTimer& timer)
{
	// const double ttt = timer.tigger_time - cur_time; // time-to-trigger
	// const size_t new_bucket_i = getBucketForTimeToTrigger(ttt);
	// addTimerToBucket(timer, new_bucket_i);

	queue.push(timer);
}


void TimerQueue::update(double cur_time, std::vector<TimerQueueTimer>& triggered_timers_out)
{
	triggered_timers_out.resize(0);

#if 1
	while(!queue.empty() && queue.top().tigger_time <= cur_time)
	{
		triggered_timers_out.push_back(queue.top());
		queue.pop();
	}
#else
	for(size_t b=0; b<8; ++b)
	{
		TimerQueueBucket& bucket = buckets[b];

		const double min_ttt = bucket_min_ttt[b];

		const double min_trigger_time = cur_time + min_ttt; // Minimum trigger time for a timer to remain in this bucket.

		for(size_t i=0; i<bucket.timers.size(); ++i)
		{
			TimerQueueTimer& timer = bucket.timers[i];
			if(timer.valid)
			{
				const double tt = timer.tigger_time;
				if(cur_time >= tt)
				{
					// Timer has triggered
					triggered_timers_out.push_back(timer);

					timer.valid = false;

					// Add to free list
					bucket.freelist.push_back((int)i);
				}
				else
				{
					if(tt < min_trigger_time)
					{
						const double ttt = tt - cur_time;
						const size_t new_bucket_i = getBucketForTimeToTrigger(ttt);
						if(new_bucket_i != b) // Check we don't try and remove and add to same bucket due to numerical issues.
						{
							// Remove from this bucket, place in other bucket
							timer.valid = false;

							// Add to free list
							bucket.freelist.push_back((int)i);

							addTimerToBucket(timer, new_bucket_i);
						}
					}
				}
			}
		}
	}
#endif
}

void TimerQueue::clear()
{
	while(!queue.empty())
		queue.pop();
}

#if 0
size_t TimerQueue::numValidTimersInBucket(size_t bucket_i)
{
	const TimerQueueBucket& bucket = buckets[bucket_i];

	size_t num = 0;
	for(size_t i=0; i<bucket.timers.size(); ++i)
		if(bucket.timers[i].valid)
			num++;
	return num;
}


size_t TimerQueue::getBucketForTimeToTrigger(double ttt)
{
	/*
	0.03125
	0.125
	0.5
	2
	8
	32
	128
	inf
	*/

	if(ttt < 2.0)
	{
		if(ttt < 0.125)
		{
			if(ttt < 0.03125)
				return 0;
			else
				return 1; // < 0.125
		}
		else
		{
			if(ttt < 0.5)
				return 2;
			else
				return 3; // < 2
		}
	}
	else // ttt >= 2
	{
		if(ttt < 32.0)
		{
			if(ttt < 8.0)
				return 4;
			else
				return 5; // < 32
		}
		else
		{
			if(ttt < 128)
				return 6;
			else
				return 7; // < inf
		}
	}
}


void TimerQueue::addTimerToBucket(const TimerQueueTimer& timer, size_t new_bucket_i)
{
	TimerQueueBucket& new_bucket = buckets[new_bucket_i];

	// Query freelist of bucket
	if(new_bucket.freelist.empty()) // If there are no free slots in bucket:
	{
		// Expand bucket
		const size_t old_size = new_bucket.timers.size();
		const size_t new_num_items = old_size;
		new_bucket.timers.resize(old_size + new_num_items);

		// Add new slots to freelist
		const size_t old_freelist_size = new_bucket.freelist.size();
		new_bucket.freelist.resize(old_freelist_size + new_num_items);
		for(size_t z=0; z<new_num_items; ++z)
			new_bucket.freelist[old_freelist_size + z] = (int)(old_size + z);
	}

	assert(!new_bucket.freelist.empty());
	const int free_index = new_bucket.freelist.back();
	new_bucket.freelist.pop_back();
	assert(free_index < (int)new_bucket.timers.size());

	new_bucket.timers[free_index] = timer;
	new_bucket.timers[free_index].valid = true;
}
#endif


#if BUILD_TESTS


#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"
#include "../utils/TestUtils.h"
#include "../maths/PCG32.h"
#include <Timer.h>


void TimerQueue::test()
{
	conPrint("TimerQueue::test()");

#if 1
	{
		TimerQueue timer_queue;

		
		TimerQueueTimer timer_a(1.0);
		timer_a.timer_id = 0;
		timer_queue.addTimer(/*cur time=*/0.0, timer_a);

		TimerQueueTimer timer_b(2.0);
		timer_b.timer_id = 1;
		timer_queue.addTimer(/*cur time=*/0.0, timer_b);
		
		std::vector<TimerQueueTimer> triggered_timers;
		timer_queue.update(/*cur_time=*/0.5, triggered_timers);
		testAssert(triggered_timers.empty());

		timer_queue.update(/*cur_time=*/1.5, triggered_timers);
		testAssert(triggered_timers.size() == 1 && triggered_timers[0].timer_id == 0);

		timer_queue.update(/*cur_time=*/1.5, triggered_timers);
		testAssert(triggered_timers.empty()); // Timer_a should have been removed already.

		timer_queue.update(/*cur_time=*/2.5, triggered_timers);
		testAssert(triggered_timers.size() == 1 && triggered_timers[0].timer_id == 1);
	}

	{
		TimerQueue timer_queue;

		PCG32 rng(1);
		for(int i=0; i<1000; ++i)
		{
			TimerQueueTimer timer_a(rng.unitRandom() * 1000.0);
			timer_queue.addTimer(/*cur time=*/0.0, timer_a);
		}

		std::vector<TimerQueueTimer> triggered_timers;
		for(int t=0; t<1000; ++t)
		{
			timer_queue.update(/*cur_time=*/(double)t, triggered_timers);
			
			for(size_t z=0; z<triggered_timers.size(); ++z)
			{
				testAssert(triggered_timers[z].tigger_time <= (double)t);
				printVar(triggered_timers[z].tigger_time);
			}
		}

		// Perf test
		Timer timer;
		const int NUM_TIMERS = 1000;
		for(int i=0; i<NUM_TIMERS; ++i)
		{
			TimerQueueTimer timer_a(rng.unitRandom() * 1000.0);
			timer_queue.addTimer(/*cur time=*/0.0, timer_a);
		}

		for(int t=0; t<NUM_TIMERS; ++t)
		{
			timer_queue.update(/*cur_time=*/(double)t, triggered_timers);
		}

		const double elapsed = timer.elapsed();
		conPrint("Adding and removing " + toString(NUM_TIMERS) + " timers took " + doubleToStringNSigFigs(elapsed * 1.0e3, 4) + " ms");
		const double time_per_timer = elapsed / NUM_TIMERS * 1.0e9;
		conPrint("time_per_timer (ns): " + toString(time_per_timer));

	}

#else
	{
		TimerQueue timer_queue;

		
		timer_queue.addTimer(/*cur time=*/0.0, TimerQueueTimer(1.0)); // Should insert in bucket 3.
		testAssert(timer_queue.numValidTimersInBucket(/*bucket_i=*/0) == 0);
		testAssert(timer_queue.numValidTimersInBucket(/*bucket_i=*/3) == 1);

		std::vector<TimerQueueTimer> triggered_timers;
		timer_queue.update(/*cur_time=*/0.6, triggered_timers); // time-to-trigger will be 0.4, so should move to bucket 2.
		testAssert(timer_queue.numValidTimersInBucket(/*bucket_i=*/2) == 1);
		testAssert(timer_queue.numValidTimersInBucket(/*bucket_i=*/3) == 0);

		testAssert(triggered_timers.empty());
	}
#endif

	conPrint("TimerQueue::test() done");
}


#endif // BUILD_TESTS
