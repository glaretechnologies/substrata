/*=====================================================================
LuaScriptEvaluator.cpp
----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LuaScriptEvaluator.h"


#include "SubstrataLuaVM.h"
#include "WorldObject.h"
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <lualib.h>


LuaScriptEvaluator::LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm_, LuaScriptOutputHandler* script_output_handler, const std::string& base_cyberspace_path, 
	const std::string& script_src, WorldObject* world_object_)
:	substrata_lua_vm(substrata_lua_vm_),
	hit_error(false),
	last_user_touched_object_exec_time(-1000),
	world_object(world_object_),
	next_timer_id(0)
{
	for(int i=0; i<MAX_NUM_TIMERS; ++i)
		timers[i].id = -1;

	LuaScriptOptions options;
	options.max_num_interrupts = 10000;
	options.script_output_handler = script_output_handler;
	options.userdata = this;
	lua_script.set(new LuaScript(substrata_lua_vm->lua_vm.ptr(), options, script_src));
	lua_script->exec();
}


LuaScriptEvaluator::~LuaScriptEvaluator()
{
}


void LuaScriptEvaluator::onUserTouchedObject(double cur_time)
{
	conPrint("onUserTouchedObject");
	if(hit_error)
		return;

	const double time_since_last_exec = cur_time - last_user_touched_object_exec_time;
	if(time_since_last_exec < 0.5)
	{
		conPrint("waiting...");
		return;
	}

	last_user_touched_object_exec_time = cur_time;

	try
	{
		lua_getglobal(lua_script->thread_state, "onUserTouchedObject");  // Push function to be called onto stack

		// Create a table ('user') for arg 1
		lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

		// Set table UID field
		lua_pushnumber(lua_script->thread_state, -666.0);
		lua_rawsetfield(lua_script->thread_state, /*table index=*/-2, "uid"); // pops UID value from stack

		// Assign user metatable to the user table
		lua_getref(lua_script->thread_state, substrata_lua_vm->userClassMetaTable_ref); // Push UserClassMetaTable onto stack
		lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//throw glare::Exception(e.what());
		conPrint("Error while executing onUserTouchedObject: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserTouchedObject: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnTimerEvent(int onTimerEvent_ref)
{
	if(hit_error)
		return;

	try
	{
		lua_getref(lua_script->thread_state, onTimerEvent_ref);  // Push function to be called onto stack

		// Create worldObject table
		lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

		// Set table UID field
		lua_pushnumber(lua_script->thread_state, (double)world_object->uid.value());
		lua_rawsetfield(lua_script->thread_state, /*table index=*/-2, "uid"); // pops UID value from stack

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//throw glare::Exception(e.what());
		conPrint("Error while executing doOnTimerEvent: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing doOnTimerEvent: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::destroyTimer(int timer_index)
{
	// Mark slot as free
	timers[timer_index].id = -1;

	// Free reference to Lua onTimerEvent function, if valid
	if(timers[timer_index].onTimerEvent_ref != LUA_NOREF)
		lua_unref(lua_script->thread_state, timers[timer_index].onTimerEvent_ref); 
}
