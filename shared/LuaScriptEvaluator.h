/*=====================================================================
LuaScriptEvaluator.h
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <lua/LuaScript.h>
#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/WeakRefCounted.h>
#include <utils/UniqueRef.h>
#include <memory>
class SubstrataLuaVM;
class WorldObject;


/*=====================================================================
LuaScriptEvaluator
------------------
Per-WorldObject
=====================================================================*/
class LuaScriptEvaluator : public WeakRefCounted
{
public:
	LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm, LuaScriptOutputHandler* script_output_handler, const std::string& base_cyberspace_path, const std::string& script_src, 
		WorldObject* world_object);
	virtual ~LuaScriptEvaluator();


	void onUserTouchedObject(double cur_time);

	void doOnTimerEvent(int onTimerEvent_ref);

	void destroyTimer(int timer_index);

	SubstrataLuaVM* substrata_lua_vm;
	UniqueRef<LuaScript> lua_script;
	bool hit_error;

	double last_user_touched_object_exec_time;

	WorldObject* world_object;

	static const int MAX_NUM_TIMERS = 4;

	struct LuaTimerInfo
	{
		int id; // -1 means no timer.
		int onTimerEvent_ref; // Reference to Lua callback function
	};
	LuaTimerInfo timers[MAX_NUM_TIMERS];

	int next_timer_id;
};
