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
#include <lua/LuaUtils.h>
#include <lualib.h>


LuaScriptEvaluator::LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm_, LuaScriptOutputHandler* script_output_handler, 
	const std::string& script_src, WorldObject* world_object_)
:	substrata_lua_vm(substrata_lua_vm_),
	hit_error(false),
	last_onUserTouchedObject_exec_time(-1000),
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

	onUserTouchedObject_ref       = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserTouchedObject");
	onUserUsedObject_ref          = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserUsedObject");
	onUserMovedNearToObject_ref   = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserMovedNearToObject");
	onUserMovedAwayFromObject_ref = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserMovedAwayFromObject");
	onUserEnteredParcel_ref       = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserEnteredParcel");
	onUserExitedParcel_ref        = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserExitedParcel");
}


LuaScriptEvaluator::~LuaScriptEvaluator()
{
}


// Checks the Lua stack size is the same upon destruction as it was upon construction.
// Used to find bugs where we did something unbalanced with the stack.
struct LuaStackChecker
{
	LuaStackChecker(lua_State* state_) : state(state_)
	{
		initial_stack_size = lua_gettop(state);
	}
	~LuaStackChecker()
	{
		const int cur_stack_size = lua_gettop(state);
		assert(initial_stack_size == cur_stack_size);
	}

	lua_State* state;
	int initial_stack_size;
};


bool LuaScriptEvaluator::hasOnUserTouchedObjectCooledDown(double cur_time)
{
	const double time_since_last_exec = cur_time - last_onUserTouchedObject_exec_time;
	return time_since_last_exec >= 1.0;
}


void LuaScriptEvaluator::doOnUserTouchedObject(UserID client_user_id, double cur_time)
{
	//conPrint("LuaScriptEvaluator: onUserTouchedObject");
	if(hit_error || (onUserTouchedObject_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	// Jolt creates contactAdded events very fast, so limit how often we call onUserTouchedObject.
	// TODO: rate limit per user.
	//const double time_since_last_exec = cur_time - last_onUserTouchedObject_exec_time;
	//if(time_since_last_exec < 1.0)
	//{
	//	//conPrint("waiting...");
	//	return;
	//}
	if(!hasOnUserTouchedObjectCooledDown(cur_time))
		return;

	last_onUserTouchedObject_exec_time = cur_time;

	conPrint("LuaScriptEvaluator::onUserTouchedObject: executing Lua onUserTouchedObject...");

	try
	{
		lua_getref(lua_script->thread_state, onUserTouchedObject_ref); // Pushes onUserTouchedObject onto the stack.

		pushUserTableOntoStack(client_user_id);

		pushWorldObjectTableOntoStack();

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing onUserTouchedObject: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserTouchedObject: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserUsedObject(UserID client_user_id)
{
	conPrint("LuaScriptEvaluator: doOnUserUsedObject");
	if(hit_error || (onUserUsedObject_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	try
	{
		lua_getref(lua_script->thread_state, onUserUsedObject_ref); // Pushes onUserUsedObject onto the stack.
		
		pushUserTableOntoStack(client_user_id);

		pushWorldObjectTableOntoStack();

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing onUserUsedObject: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserUsedObject: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedNearToObject(UserID client_user_id)
{
	conPrint("LuaScriptEvaluator: doOnUserMovedNearToObject");
	if(hit_error || (onUserMovedNearToObject_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	try
	{
		lua_getref(lua_script->thread_state, onUserMovedNearToObject_ref); // Pushes onUserMovedNearToObject onto the stack.

		pushUserTableOntoStack(client_user_id);

		pushWorldObjectTableOntoStack();

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing onUserMovedNearToObject: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserMovedNearToObject: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedAwayFromObject(UserID client_user_id)
{
	conPrint("LuaScriptEvaluator: doOnUserMovedAwayFromObject");
	if(hit_error || (onUserMovedAwayFromObject_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	try
	{
		lua_getref(lua_script->thread_state, onUserMovedAwayFromObject_ref); // Pushes onUserMovedAwayFromObject onto the stack.

		pushUserTableOntoStack(client_user_id);

		pushWorldObjectTableOntoStack();

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing onUserMovedAwayFromObject: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserMovedAwayFromObject: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserEnteredParcel(UserID client_user_id, ParcelID parcel_id)
{
	conPrint("LuaScriptEvaluator: doOnUserEnteredParcel");
	if(hit_error || (onUserEnteredParcel_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	try
	{
		lua_getref(lua_script->thread_state, onUserEnteredParcel_ref); // Pushes onUserEnteredParcel_ref onto the stack.

		pushUserTableOntoStack(client_user_id);
		pushWorldObjectTableOntoStack();
		pushParcelTableOntoStack(parcel_id);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/3, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing doOnUserEnteredParcel: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing doOnUserEnteredParcel: " + e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserExitedParcel(UserID client_user_id, ParcelID parcel_id)
{
	conPrint("LuaScriptEvaluator: doOnUserEnteredParcel");
	if(hit_error || (onUserExitedParcel_ref == LUA_NOREF))
		return;

	LuaStackChecker checker(lua_script->thread_state);

	try
	{
		lua_getref(lua_script->thread_state, onUserExitedParcel_ref); // Pushes onUserExitedParcel_ref onto the stack.

		pushUserTableOntoStack(client_user_id);
		pushWorldObjectTableOntoStack();
		pushParcelTableOntoStack(parcel_id);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/3, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		conPrint("Error while executing onUserExitedParcel_ref: " + std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing onUserExitedParcel_ref: " + e.what());
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

		pushWorldObjectTableOntoStack();

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


void LuaScriptEvaluator::pushUserTableOntoStack(UserID client_user_id)
{
	// Create a table ('user') for arg 1
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set table UserID field
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "id", client_user_id.value());

	// Assign user metatable to the user table
	lua_getref(lua_script->thread_state, substrata_lua_vm->userClassMetaTable_ref); // Push UserClassMetaTable onto stack
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."
}


void LuaScriptEvaluator::pushWorldObjectTableOntoStack()
{
	// Create worldObject table
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	lua_getref(lua_script->thread_state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setLightUserDataAsTableField(lua_script->thread_state, "uid", (void*)world_object->uid.value());
}


void LuaScriptEvaluator::pushParcelTableOntoStack(ParcelID parcel_id)
{
	// Create parcel table
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	//lua_getref(lua_script->thread_state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	//lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setLightUserDataAsTableField(lua_script->thread_state, "uid", (void*)parcel_id.value());
}
