/*=====================================================================
ScriptTimerQueue.h
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <lua/LuaVM.h>
#include <maths/Vec4f.h>
#include <maths/PCG32.h>
#include <utils/RefCounted.h>
#include <utils/WeakReference.h>
#include <utils/GenerationalArray.h>
#include <string>
#include <vector>
#include <queue>


class LuaScript;
class SubstrataLuaScript;


class ScriptTimerQueueTimer
{
public:
	ScriptTimerQueueTimer();
	ScriptTimerQueueTimer(double tigger_time_);

	double tigger_time;
	//bool valid;

	LuaVM::LuaValueRef onTimerEvent_ref; // Reference to Lua function
	bool repeating;
	double period;
	int timer_index;
	int timer_id;
	WeakReference<SubstrataLuaScript> sub_lua_script;
};


class TimerQueueBucket
{
public:
	std::vector<ScriptTimerQueueTimer> timers;
	std::vector<int> freelist;
};


/*=====================================================================
ScriptTimerQueue
----------------
Handles timer events for Lua scripts.
Currently using priority_queue.
There is also some commented-out and unfinished code for a bucket-based solution
that may be more efficient.
=====================================================================*/
class ScriptTimerQueue
{
public:
	ScriptTimerQueue();
	~ScriptTimerQueue();

	void addTimer(double cur_time, const ScriptTimerQueueTimer& timer);
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

	void update(double cur_time, std::vector<ScriptTimerQueueTimer>& triggered_timers_out);

	// Returns the time a new timer should first trigger.  For repeating timers, this is delayed by a random amount of up to the period or 1 second, whichever is smaller,
	// so that repeating timers created at the same time (e.g. when a world loads) don't all trigger in the same frame.
	double getFirstTriggerTime(double cur_time, double period, bool repeating);

	// Re-adds a repeating timer that has just triggered.  The next trigger time is one period after the previous trigger time, so that the timer keeps its phase,
	// and timers that triggered in the same frame don't become synchronised.  If the timer is more than a period behind, missed triggers are skipped.
	void reAddRepeatingTimer(double cur_time, ScriptTimerQueueTimer& timer);

	void clear(); // Just used for testing

	static void test();

private:
	//size_t numValidTimersInBucket(size_t bucket_i);
	//size_t getBucketForTimeToTrigger(double ttt);
	//void addTimerToBucket(const TimerQueueTimer& timer, size_t new_bucket_i);

	//TimerQueueBucket buckets[8];
	//double bucket_min_ttt[8];
	//double bucket_max_ttt[8];

	struct ScriptTimerComparator
	{
		bool operator() (const ScriptTimerQueueTimer& a, const ScriptTimerQueueTimer& b) const { return a.tigger_time > b.tigger_time; }
	};

	std::priority_queue<ScriptTimerQueueTimer, std::vector<ScriptTimerQueueTimer>, ScriptTimerComparator> queue;

	PCG32 rng;
};
