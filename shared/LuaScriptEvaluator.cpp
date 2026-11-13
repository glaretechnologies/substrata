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
#include "FileTypes.h"
#include "RateLimiter.h"
#include "../server/LuaHTTPRequestManager.h" // For LuaHTTPRequestResult
#if GUI_CLIENT
#include "../gui_client/GUIClient.h"
#include "../gui_client/LuaScriptUI.h"
#endif
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/Lock.h>
#include <utils/Parser.h>
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


LuaScriptEvaluator::LuaScriptEvaluator(const Reference<SubstrataLuaVM>& substrata_lua_vm_, LuaScriptOutputHandler* script_output_handler_, 
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
	cur_world_state_lock(nullptr),
	calling_avatar_uid(UID::invalidUID())
{
	// Parse "--@resource URL" lines from script, save in resource_URLs.
	appendScriptResourceURLs(script_src, /*allocator=*/nullptr, /*urls out=*/this->resource_URLs);

#if SERVER
	play_sound_rate_limiter = new RateLimiter(/*period (s)=*/1.0, /*max num in period=*/10);
#endif


	for(int i=0; i<MAX_NUM_TIMERS; ++i)
		timers[i].id = -1;

	LuaScriptOptions options;
	options.max_num_interrupts = 10000;
	options.script_output_handler = script_output_handler_;
	options.userdata = this;
	lua_script.set(new LuaScript(substrata_lua_vm->lua_vm.ptr(), options, script_src));

	try
	{
		// Set 'this_object' global variable
		pushWorldObjectTableOntoStack(lua_script->thread_state, world_object->uid);
		lua_setglobal(lua_script->thread_state, "this_object");

		// Set 'IS_SERVER' global variable.
#if SERVER
		lua_pushboolean(lua_script->thread_state, 1);
#else
		lua_pushboolean(lua_script->thread_state, 0);
#endif
		lua_setglobal(lua_script->thread_state, "IS_SERVER");

		// Set 'IS_CLIENT' global variable.
#if SERVER
		lua_pushboolean(lua_script->thread_state, 0);
#else
		lua_pushboolean(lua_script->thread_state, 1);
#endif
		lua_setglobal(lua_script->thread_state, "IS_CLIENT");
	}
	catch(std::exception& e) // Lua memory alloc failures throw lua_exception which derives from std::exception.  Handle them.
	{
		throw glare::Exception(std::string(e.what()));
	}
}


LuaScriptEvaluator::~LuaScriptEvaluator()
{
	// Destroy any timers
	for(int i=0; i<MAX_NUM_TIMERS; ++i)
		if(timers[i].id != -1)
			destroyTimer(i, timers[i].id);

	removeScriptUIWidgets();
}


void LuaScriptEvaluator::exec(WorldStateLock& world_state_lock)
{
	SetCurWorldStateLockClass lock_setter(this, world_state_lock);
	
	try
	{
		lua_script->exec();

		// Add any event handling functions defined in the script to the object event-handler list.
		// Event handling functions defined in this way basically do implicit addEventListener() calls.
		{
			const LuaScript::LuaFuncRefAndPtr func_info =  lua_script->getRefToFunction("onUserTouchedObject");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserTouchedObject_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref); // if we failed to add the handler to onUserTouchedObject_handlers, remove the reference to the Lua func here.
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onUserUsedObject");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserUsedObject_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onUserMovedNearToObject");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onUserMovedAwayFromObject");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onUserEnteredParcel");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onUserExitedParcel");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onUserExitedParcel_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
		{
			const LuaScript::LuaFuncRefAndPtr func_info = lua_script->getRefToFunction("onChatMessage");
			if(func_info.ref.ref != LUA_NOREF)
			{
				HandlerFunc handler_func({WeakReference<LuaScriptEvaluator>(this), func_info.ref, func_info.func_ptr});
				if(!world_object->getOrCreateEventHandlers()->onChatMessage_handlers.addHandler(handler_func))
					lua_script->freeRefToFunction(handler_func.handler_func_ref);
			}
		}
	}
	catch(glare::Exception&)
	{
		hit_error = true;
		lua_script->addExecutionTimeToVMAndResetCounter();
		throw;
	}
	catch(std::exception& e) // Lua memory alloc failures throw lua_exception which derives from std::exception.  Handle them.
	{
		hit_error = true;
		lua_script->addExecutionTimeToVMAndResetCounter();
		throw glare::Exception(std::string(e.what()));
	}

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::removeScriptUIWidgets()
{
#if GUI_CLIENT
	if(substrata_lua_vm->gui_client && substrata_lua_vm->gui_client->lua_script_ui)
		substrata_lua_vm->gui_client->lua_script_ui->removeWidgetsForScript(this);
#endif
}


void LuaScriptEvaluator::doOnUserTouchedObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserTouchedObject");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes function onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);

		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserUsedObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserUsedObject");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes onUserUsedObject onto the stack.
		
		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);

		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserMovedNearToObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedNearToObject");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);

		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserMovedAwayFromObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserMovedAwayFromObject");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);

		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserEnteredParcel(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserEnteredParcel");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);
		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);
		pushParcelTableOntoStack(lua_script->thread_state, parcel_id);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserExitedParcel(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept
{
	//conPrint("LuaScriptEvaluator: doOnUserExitedParcel");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);
		pushWorldObjectTableOntoStack(lua_script->thread_state, ob_uid);
		pushParcelTableOntoStack(lua_script->thread_state, parcel_id);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserEnteredVehicle(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept
{
	// conPrint("LuaScriptEvaluator: doOnUserEnteredVehicle");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);
		pushWorldObjectTableOntoStack(lua_script->thread_state, vehicle_ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUserExitedVehicle(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept
{
	// conPrint("LuaScriptEvaluator: doOnUserExitedVehicle");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);
		pushWorldObjectTableOntoStack(lua_script->thread_state, vehicle_ob_uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnChatMessage(LuaVM::LuaValueRef func_ref, UID avatar_uid, const std::string& message, WorldStateLock& world_state_lock) noexcept
{
	// conPrint("LuaScriptEvaluator: doOnChatMessage");
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid;

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Pushes func_ref onto the stack.

		pushAvatarTableOntoStack(lua_script->thread_state, avatar_uid);

		LuaUtils::pushString(lua_script->thread_state, message);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnTimerEvent(LuaVM::LuaValueRef onTimerEvent_ref, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = UID::invalidUID();

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, onTimerEvent_ref.ref);  // Push function to be called onto stack

		pushWorldObjectTableOntoStack(lua_script->thread_state, this->world_object->uid);

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUIEvent(LuaVM::LuaValueRef func_ref, UID avatar_uid, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid; // So that e.g. playSound() in the callback plays the sound locally.

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Push function to be called onto stack

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/0, /*nresults=*/0);
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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::doOnUIValueChangedEvent(LuaVM::LuaValueRef func_ref, double value, UID avatar_uid, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error || (func_ref.ref == LUA_NOREF))
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = avatar_uid; // So that e.g. playSound() in the callback plays the sound locally.

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, func_ref.ref); // Push function to be called onto stack

		lua_pushnumber(lua_script->thread_state, value);

		// Call function
		lua_call(lua_script->thread_state, /*nargs=*/1, /*nresults=*/0);
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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


void LuaScriptEvaluator::destroyTimer(int timer_index, int timer_id)
{
	if(timers[timer_index].id == timer_id)
	{
		// Mark slot as free
		timers[timer_index].id = -1;

		// Free reference to Lua onTimerEvent function, if valid
		if(timers[timer_index].onTimerEvent_ref.ref != LUA_NOREF)
		{
			lua_script->freeRefToFunction(timers[timer_index].onTimerEvent_ref);
			timers[timer_index].onTimerEvent_ref = LuaVM::LuaValueRef();
		}
	}
}


// See doHTTPGetRequestAsync in SubstrataLuaVM.cpp
void LuaScriptEvaluator::doOnError(LuaVM::LuaValueRef onError_ref, int error_code, const std::string& error_description, WorldStateLock& world_state_lock) noexcept
{
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = UID::invalidUID();

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, onError_ref.ref);  // Push function to be called onto stack

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

	lua_script->addExecutionTimeToVMAndResetCounter();
}


// See doHTTPGetRequestAsync in SubstrataLuaVM.cpp
void LuaScriptEvaluator::doOnDone(LuaVM::LuaValueRef onDone_ref, Reference<LuaHTTPRequestResult> result, WorldStateLock& world_state_lock) noexcept
{
#if SERVER
	if(hit_error)
		return;

	try
	{
		SetCurWorldStateLockClass setter(this, world_state_lock);
		this->calling_avatar_uid = UID::invalidUID();

		assert(lua_script->num_interrupts == 0);

		lua_getref(lua_script->thread_state, onDone_ref.ref);  // Push function to be called onto stack

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

	lua_script->addExecutionTimeToVMAndResetCounter();
#else
	assert(0);
#endif
}


static inline bool isLineEndOrEOF(const Parser& parser) { return parser.eof() || (parser.current() == '\r') || (parser.current() == '\n'); }


void LuaScriptEvaluator::appendScriptResourceURLs(const string_view script, glare::ArenaAllocator* allocator, DependencyURLVector& urls_out)
{
	Parser parser(script.data(), script.size());
	if(!parser.parseString("--lua"))
		return;
	parser.advancePastLine();

	const size_t initial_size = urls_out.size(); // urls_out may already contain other dependency URLs, so count only the ones added here.

	while(parser.notEOF() && ((urls_out.size() - initial_size) < MAX_NUM_RESOURCE_URLS))
	{
		parser.parseSpacesAndTabs();
		if(!isLineEndOrEOF(parser)) // Blank lines are part of the header.
		{
			if(!parser.parseString("--"))
				break; // A line that isn't a comment ends the header.

			parser.parseSpacesAndTabs();
			if(parser.parseString("@resource"))
			{
				if(parser.currentIsChar(' ') || parser.currentIsChar('\t'))
				{
					parser.parseSpacesAndTabs();
					string_view URL;
					parser.parseNonWSToken(URL);
					// URLs longer than MAX_URL_SIZE are ignored: clients reject longer URLs in ScriptPlaySound messages.
					if(!URL.empty() && (URL.size() <= WorldObject::MAX_URL_SIZE))
						urls_out.push_back(DependencyURL(URLString(URL, glare::STLArenaAllocator<char>(allocator))));
				}
			}
		}

		parser.advancePastLine();
	}
}


bool LuaScriptEvaluator::addScriptResourceDeclaration(std::string& script, const string_view URL)
{
	const std::string declaration = "-- @resource " + std::string(URL) + "\n";

	if(script.empty())
	{
		script = "--lua\n" + declaration;
		return true;
	}

	Parser parser(script.data(), script.size());
	if(!parser.parseString("--lua"))
		return false;
	parser.advancePastLine();

	// Insert after the last @resource line in the header, or straight after the '--lua' line if there are none.
	size_t insert_pos = parser.currentPos();
	while(parser.notEOF())
	{
		parser.parseSpacesAndTabs();
		if(!isLineEndOrEOF(parser)) // Blank lines are part of the header.
		{
			if(!parser.parseString("--"))
				break; // A line that isn't a comment ends the header.

			parser.parseSpacesAndTabs();
			const bool is_resource_line = parser.parseString("@resource");
			parser.advancePastLine();
			if(is_resource_line)
				insert_pos = parser.currentPos();
		}
		else
			parser.advancePastLine();
	}

	if((insert_pos == script.size()) && (script.back() != '\n')) // If inserting after a last line that has no line ending:
	{
		script += "\n";
		insert_pos = script.size();
	}

	script.insert(insert_pos, declaration);
	return true;
}


bool LuaScriptEvaluator::isResourceURLDeclared(const string_view URL) const
{
	for(size_t i=0; i<resource_URLs.size(); ++i)
		if(resource_URLs[i].URL == URL)
			return true;
	return false;
}


void LuaScriptEvaluator::warnAboutUndeclaredResourceURL(const string_view func_name, const string_view URL)
{
	// Don't store or copy URLs that can't be valid resource URLs, as a script can pass arbitrarily large strings.
	if(URL.size() > WorldObject::MAX_URL_SIZE)
		return;

	const size_t max_num_warned_URLs = 16; // Bound the memory used, for scripts that construct many different URLs.
	if(warned_undeclared_resource_URLs.size() >= max_num_warned_URLs)
		return;

	const bool inserted = warned_undeclared_resource_URLs.insert(std::string(URL)).second;
	if(inserted && script_output_handler)
	{
		const std::string msg = "Warning: " + std::string(func_name) + "(): '" + std::string(URL) + "' is not declared in the script header.  Declare it with a line like:\n-- @resource " + std::string(URL);
		script_output_handler->printFromLuaScript(lua_script.ptr(), msg.data(), msg.size());
	}
}


void LuaScriptEvaluator::pushUserTableOntoStack(lua_State* state, UserID client_user_id)
{
	// Create a table ('user') for arg 1
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set table UserID field
	LuaUtils::setNumberAsTableField(state, "uid", client_user_id.value()); // NOTE: Call it "uid" for consistency with object, parcel etc.

	// Assign user metatable to the user table
	lua_getref(state, substrata_lua_vm->userClassMetaTable_ref.ref); // Push UserClassMetaTable onto stack
	lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."
}


void LuaScriptEvaluator::pushAvatarTableOntoStack(lua_State* state, UID avatar_uid)
{
	// Create a table ('avatar') for arg 1
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set table uid field
	// NOTE: Actually using avatar id
	LuaUtils::setNumberAsTableField(state, "uid", (double)avatar_uid.value());

	// Assign avatar metatable to the avatar table
	lua_getref(state, substrata_lua_vm->avatarClassMetaTable_ref.ref); // Push AvatarClassMetaTable onto stack
	lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."
}


void LuaScriptEvaluator::pushWorldObjectTableOntoStack(lua_State* state, UID ob_uid)
{
	// Create worldObject table
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	lua_getref(state, substrata_lua_vm->worldObjectClassMetaTable_ref.ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setNumberAsTableField(state, "uid", (double)ob_uid.value());
}


void LuaScriptEvaluator::pushParcelTableOntoStack(lua_State* state, ParcelID parcel_id)
{
	// Create parcel table
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/1); // Create table

	// Set metatable to worldObjectClassMetaTable
	//lua_getref(state, substrata_lua_vm->worldObjectClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	//lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	// Set table UID field
	LuaUtils::setNumberAsTableField(state, "uid", (double)parcel_id.value());
}


#if BUILD_TESTS


#include <utils/TestUtils.h>


static std::vector<std::string> getScriptResourceURLsAsStrings(const std::string& script)
{
	DependencyURLVector urls;
	LuaScriptEvaluator::appendScriptResourceURLs(script, /*allocator=*/nullptr, urls);

	std::vector<std::string> res;
	for(size_t i=0; i<urls.size(); ++i)
		res.push_back(toStdString(urls[i].URL));
	return res;
}


void LuaScriptEvaluator::test()
{
#if 1
	// Scripts without a header
	testAssert(getScriptResourceURLsAsStrings("").empty());
	testAssert(getScriptResourceURLsAsStrings("--lua").empty());
	testAssert(getScriptResourceURLsAsStrings("--lua\n").empty());
	testAssert(getScriptResourceURLsAsStrings("-- @resource a.mp3").empty()); // Not a Lua script
	testAssert(getScriptResourceURLsAsStrings("<?xml version=\"1.0\"?>\n-- @resource a.mp3").empty());

	// Declarations end at the first line that isn't blank or a comment
	{
		const std::vector<std::string> URLs = getScriptResourceURLsAsStrings("--lua\n-- @resource a_123.mp3\n-- @resource b_456.wav\nprint(1)\n-- @resource c.mp3");
		testAssert(URLs.size() == 2 && URLs[0] == "a_123.mp3" && URLs[1] == "b_456.wav");
	}

	// CRLF line endings, blank lines, other comments, and spaces and tabs around the parts of a declaration
	{
		const std::vector<std::string> URLs = getScriptResourceURLsAsStrings("--lua\r\n\r\n  --\t@resource\ta.mp3  \r\n-- some comment\r\n--@resource b.mp3\r\nx = 1");
		testAssert(URLs.size() == 2 && URLs[0] == "a.mp3" && URLs[1] == "b.mp3");
	}

	// Last line without a line ending
	{
		const std::vector<std::string> URLs = getScriptResourceURLsAsStrings("--lua\n-- @resource a.mp3");
		testAssert(URLs.size() == 1 && URLs[0] == "a.mp3");
	}

	// URLs longer than WorldObject::MAX_URL_SIZE are ignored
	{
		const std::string max_len_URL = std::string(WorldObject::MAX_URL_SIZE - 4, 'a') + ".mp3";
		const std::string too_long_URL = std::string(WorldObject::MAX_URL_SIZE - 3, 'a') + ".mp3";
		const std::vector<std::string> URLs = getScriptResourceURLsAsStrings("--lua\n-- @resource " + too_long_URL + "\n-- @resource " + max_len_URL + "\n");
		testAssert(URLs.size() == 1 && URLs[0] == max_len_URL);
	}

	// Number of declarations is capped
	{
		std::string script = "--lua\n";
		for(int i=0; i<(int)MAX_NUM_RESOURCE_URLS + 10; ++i)
			script += "-- @resource s" + toString(i) + ".mp3\n";
		testAssert(getScriptResourceURLsAsStrings(script).size() == MAX_NUM_RESOURCE_URLS);
	}
#endif
}


#endif // BUILD_TESTS
