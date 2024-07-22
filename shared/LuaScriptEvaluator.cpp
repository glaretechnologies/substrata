/*=====================================================================
LuaScriptEvaluator.cpp
----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LuaScriptEvaluator.h"


#include "ObjectEventHandlers.h"
#include "SubstrataLuaVM.h"
#include "WorldStateLock.h"
#include "WorldObject.h"
#include "../server/LuaHTTPRequestManager.h" // For LuaHTTPRequestResult
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/Lock.h>
#include <lua/LuaUtils.h>
#include <lualib.h>


// Sets script_evaluator->cur_world_state_lock pointer to the world_state_lock address for the lifetime of the object.
// This is so functions that are called from lua code can check that we hold the world state lock.
class SetCurWorldStateLockClass
{
public:
	SetCurWorldStateLockClass(LuaScriptEvaluator* script_evaluator_, WorldStateLock& world_state_lock)
	:	script_evaluator(script_evaluator_)
	{
		script_evaluator_->cur_world_state_lock = &world_state_lock;
	}

	~SetCurWorldStateLockClass()
	{
		script_evaluator->cur_world_state_lock = nullptr;
	}

private:
	LuaScriptEvaluator* script_evaluator;
};


LuaScriptEvaluator::LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm_, LuaScriptOutputHandler* script_output_handler_, 
	const std::string& script_src, WorldObject* world_object_,
#if SERVER
		ServerWorldState* world_state_, // The world that the object belongs to.
#endif
	WorldStateLock& world_state_lock
)
:	substrata_lua_vm(substrata_lua_vm_),
	script_output_handler(script_output_handler_),
	hit_error(false),
	world_object(world_object_),
#if SERVER
	world_state(world_state_),
#endif
	next_timer_id(0),
	num_obs_event_listening(0),
	cur_world_state_lock(nullptr)
{
	for(int i=0; i<MAX_NUM_TIMERS; ++i)
		timers[i].id = -1;

	LuaScriptOptions options;
	options.max_num_interrupts = 10000;
	options.script_output_handler = script_output_handler_;
	options.userdata = this;
	lua_script.set(new LuaScript(substrata_lua_vm->lua_vm.ptr(), options, script_src));

	SetCurWorldStateLockClass lock_setter(this, world_state_lock);
	lua_script->exec();

	// Add any event handling functions defined in the script to the object event-handler list.
	// Event handling functions defined in this way basically do implicit addEventListener() calls.
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserTouchedObject");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserTouchedObject_handlers.addHandler(handler_func);
		}
	}
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserUsedObject");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserUsedObject_handlers.addHandler(handler_func);
		}
	}
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserMovedNearToObject");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.addHandler(handler_func);
		}
	}
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserMovedAwayFromObject");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.addHandler(handler_func);
		}
	}
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserEnteredParcel");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.addHandler(handler_func);
		}
	}
	{
		const LuaUtils::LuaFuncRefAndPtr func_info = LuaUtils::getRefToFunction(lua_script->thread_state, "onUserExitedParcel");
		if(func_info.ref != LUA_NOREF)
		{
			HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
			world_object->getOrCreateEventHandlers()->onUserExitedParcel_handlers.addHandler(handler_func);
		}
	}
}


LuaScriptEvaluator::~LuaScriptEvaluator()
{
}


void LuaScriptEvaluator::doOnUserTouchedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserTouchedObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserTouchedObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), e.what());
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserUsedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserUsedObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserUsedObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedNearToObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedNearToObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserMovedNearToObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserMovedAwayFromObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedAwayFromObject");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserMovedAwayFromObject: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserEnteredParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserEnteredParcel");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnUserEnteredParcel: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserExitedParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserExitedParcel");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing onUserExitedParcel: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserEnteredVehicle(int func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept
{
	// conPrint("LuaScriptEvaluator: doOnUserEnteredVehicle");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);
		pushWorldObjectTableOntoStack(vehicle_ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnUserExitedVehicle(int func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept
{
	// conPrint("LuaScriptEvaluator: doOnUserExitedVehicle");
	if(hit_error || (func_ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, func_ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(avatar_uid);
		pushWorldObjectTableOntoStack(vehicle_ob_uid);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/2, /*nresults=*/0); // Pops all arguments and function value
	}
	catch(std::exception& e)
	{
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


void LuaScriptEvaluator::doOnTimerEvent(int onTimerEvent_ref, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

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
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnTimerEvent: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
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


// See doHTTPGetRequestAsync in SubstrataLuaVM.cpp
void LuaScriptEvaluator::doOnError(int onError_ref, int error_code, const std::string& error_description, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, onError_ref);  // Push function to be called onto stack

		// onError gets passed
		// {
		//	  error_code : number
		//	  error_description : string
		// }
		lua_newtable(lua_script->thread_state);
		LuaUtils::setNumberAsTableField(lua_script->thread_state, "error_code", error_code);
		LuaUtils::setStringAsTableField(lua_script->thread_state, "error_description", error_description);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing doOnError: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnError: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
}


// See doHTTPGetRequestAsync in SubstrataLuaVM.cpp
void LuaScriptEvaluator::doOnDone(int onDone_ref, Reference<LuaHTTPRequestResult> result, WorldStateLock& world_state_lock) noexcept
{
#if SERVER
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);

		lua_script->resetExecutionTimeCounter();

		lua_getref(lua_script->thread_state, onDone_ref);  // Push function to be called onto stack

		// onDone gets passed:
		// 
		// {
		//   response_code: number
		//   response_message : string
		//   mime_type : string
		//   body_data : buffer
		// }
		lua_newtable(lua_script->thread_state);
		LuaUtils::setNumberAsTableField(lua_script->thread_state, "response_code", result->response.response_code);
		LuaUtils::setStringAsTableField(lua_script->thread_state, "response_message", result->response.response_message);
		LuaUtils::setStringAsTableField(lua_script->thread_state, "mime_type", result->response.mime_type);

		// Create lua buffer, set as body_data table field.
		void* lua_buf = lua_newbuffer(lua_script->thread_state, /*size=*/result->data.size());
		if(result->data.size() > 0)
			std::memcpy(lua_buf, result->data.data(), result->data.size());

		lua_rawsetfield(lua_script->thread_state, /*table index=*/-2, "body_data");


		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
	}
	catch(std::exception& e)
	{
		//conPrint("Error while executing doOnDone: " + std::string(e.what()));
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
	catch(glare::Exception& e)
	{
		//conPrint("Error while executing doOnDone: " + e.what());
		if(script_output_handler)
			script_output_handler->errorOccurredFromLuaScript(lua_script.ptr(), std::string(e.what()));
		hit_error = true;
	}
#else
	assert(0);
#endif
}


void LuaScriptEvaluator::pushUserTableOntoStack(UserID client_user_id)
{
	// Create a table ('user') for arg 1
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set table UserID field
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


void LuaScriptEvaluator::pushWorldObjectTableOntoStack(UID ob_uid)
{
	// Create worldObject table
	lua_createtable(lua_script->thread_state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	lua_getref(lua_script->thread_state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(lua_script->thread_state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setNumberAsTableField(lua_script->thread_state, "uid", (double)ob_uid.value());
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
