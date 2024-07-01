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


LuaScriptEvaluator::LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm_, LuaScriptOutputHandler* script_output_handler_, 
	const std::string& script_src, WorldObject* world_object_
#if SERVER
		,ServerWorldState* world_state_ // The world that the object belongs to.
#endif
)
:	substrata_lua_vm(substrata_lua_vm_),
	script_output_handler(script_output_handler_),
	hit_error(false),
	world_object(world_object_),
#if SERVER
	world_state(world_state_),
#endif
	next_timer_id(0)
{
	for(int i=0; i<MAX_NUM_TIMERS; ++i)
		timers[i].id = -1;

	onUserTouchedObject_ref       = LUA_NOREF;
	onUserUsedObject_ref          = LUA_NOREF;
	onUserMovedNearToObject_ref   = LUA_NOREF;
	onUserMovedAwayFromObject_ref = LUA_NOREF;
	onUserEnteredParcel_ref       = LUA_NOREF;
	onUserExitedParcel_ref        = LUA_NOREF;

	LuaScriptOptions options;
	options.max_num_interrupts = 10000;
	options.script_output_handler = script_output_handler_;
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


void LuaScriptEvaluator::doOnUserTouchedObject(int func_ref, UID avatar_uid, UID ob_uid, double cur_time) noexcept
{
	//conPrint("LuaScriptEvaluator: onUserTouchedObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes function onto the stack.

		pushAvatarTableOntoStack(avatar_uid);

		pushWorldObjectTableOntoStack(ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing onUserTouchedObject: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserTouchedObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserUsedObject(int func_ref, UID avatar_uid, UID ob_uid) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserUsedObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes onUserUsedObject onto the stack.
		
		pushAvatarTableOntoStack(avatar_uid);

		pushWorldObjectTableOntoStack(ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing onUserUsedObject: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserUsedObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedNearToObject(int func_ref, UID avatar_uid, UID ob_uid) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedNearToObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);

		pushWorldObjectTableOntoStack(ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing onUserMovedNearToObject: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserMovedNearToObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedAwayFromObject(int func_ref, UID avatar_uid, UID ob_uid) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedAwayFromObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);

		pushWorldObjectTableOntoStack(ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing onUserMovedAwayFromObject: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserMovedAwayFromObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserEnteredParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserEnteredParcel");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);
		pushWorldObjectTableOntoStack(ob_uid);
		pushParcelTableOntoStack(parcel_id);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/3, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing doOnUserEnteredParcel: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnUserEnteredParcel: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserExitedParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserExitedParcel");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);
		pushWorldObjectTableOntoStack(ob_uid);
		pushParcelTableOntoStack(parcel_id);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/3, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing onUserExitedParcel: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserExitedParcel: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnTimerEvent(int onTimerEvent_ref) noexcept
{
	if(hit_error)
		return;

	try
	{
		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, onTimerEvent_ref);  // Push function to be called onto stack

		pushWorldObjectTableOntoStack(this->world_object->uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing doOnTimerEvent: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnTimerEvent: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurred(lua_script.ptr(), std::string(e.what()));
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
	// NOTE: Actually using avatar id
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "uid", client_user_id.value()); // NOTE: Call it "uid" for consistency with object, parcel etc.

	// Assign user metatable to the user table
	lua_getref(lua_script->thread_state, substrata_lua_vm->userClassMetaTable_ref); // Push UserClassMetaTable onto stack
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."
}


void LuaScriptEvaluator::pushAvatarTableOntoStack(UID avatar_uid)
{
	// Create a table ('avatar') for arg 1
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set table uid field
	// NOTE: Actually using avatar id
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "uid", (double)avatar_uid.value());

	// Assign avatar metatable to the avatar table
	lua_getref(lua_script->thread_state, substrata_lua_vm->avatarClassMetaTable_ref); // Push AvatarClassMetaTable onto stack
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."
}


void LuaScriptEvaluator::pushWorldObjectTableOntoStack(UID uid)
{
	// Create worldObject table
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	lua_getref(lua_script->thread_state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "uid", (double)uid.value());
}


void LuaScriptEvaluator::pushParcelTableOntoStack(ParcelID parcel_id)
{
	// Create parcel table
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	//lua_getref(lua_script->thread_state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	//lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "uid", (double)parcel_id.value());
}
