/*=====================================================================
TimerQueue.h
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/WeakReference.h>
#include <utils/GenerationalArray.h>
#include <string>
#include <vector>
#include <queue>


class LuaScript;
class LuaScriptEvaluator;


class TimerQueueTimer
{
public:
	TimerQueueTimer();
	TimerQueueTimer(double tigger_time_);

	double tigger_time;
	//bool valid;

	int onTimerEvent_ref; // Reference to Lua function
	bool repeating;
	double period;
	int timer_index;
	int timer_id;
	WeakReference<LuaScriptEvaluator> lua_script_evaluator;
};


class TimerQueueBucket
{
public:
	std::vector<TimerQueueTimer> timers;
	std::vector<int> freelist;
};


/*=====================================================================
TimerQueue
----------
Handles timer events for Lua scripts.
Currently using priority_queue.
There is also some commented-out and unfinished code for a bucket-based solution
that may be more efficient.
=====================================================================*/
class TimerQueue
{
public:
	TimerQueue();
	~TimerQueue();

	void addTimer(double cur_time, const TimerQueueTimer& timer);
	/*
	segment 0: time to trigger < 0.03125
	0.125
	0.5
	2
	8
	32
	128
	inf
	*/

	void update(double cur_time, std::vector<TimerQueueTimer>& triggered_timers_out);

	void clear(); // Just used for testing

	static void test();

private:
	//size_t numValidTimersInBucket(size_t bucket_i);
	//size_t getBucketForTimeToTrigger(double ttt);
	//void addTimerToBucket(const TimerQueueTimer& timer, size_t new_bucket_i);

	//TimerQueueBucket buckets[8];
	//double bucket_min_ttt[8];
	//double bucket_max_ttt[8];

	struct TimerComparator
	{
		bool operator() (const TimerQueueTimer& a, const TimerQueueTimer& b) const { return a.tigger_time > b.tigger_time; }
	};

	std::priority_queue<TimerQueueTimer, std::vector<TimerQueueTimer>, TimerComparator> queue;
};
