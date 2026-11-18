/*=====================================================================
ServerLuaScriptTests.cpp
------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ServerLuaScriptTests.h"


#if BUILD_TESTS


#include "Server.h"
#include "LuaHTTPRequestManager.h"
#include "ServerWorldState.h"
#include "../shared/SubstrataLuaScript.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/WorldObject.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/WorldStateLock.h"
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/StringUtils.h>
#include <utils/TestUtils.h>
#include <utils/TestExceptionUtils.h>
#include <utils/Timer.h>
#include <lualib.h>


class ServerTestLuaScriptOutputHandler : public LuaScriptOutputHandler
{
public:
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override
	{
		conPrint("Test Lua: " + std::string(s, len));
		buf += std::string(s, len);
	}

	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override
	{
		conPrint("Test Lua error: " + msg);
		buf += msg;
	}

	std::string buf;
};


void ServerLuaScriptTests::test()
{
#if SERVER
	try
	{
		Server server;

		WorldStateLock lock(server.world_state->mutex);

		Reference<ServerWorldState> main_world_state = new ServerWorldState();
		server.world_state->world_states[""] = main_world_state;


		SubstrataLuaVM::SubstrataLuaVMArgs args(&server);
		SubstrataLuaVM vm(args);
		vm.setAsNotIndependentlyHeapAllocated();

		const int initial_num_lua_value_refs = vm.getNumLuaValueReferences();

		ServerTestLuaScriptOutputHandler output_handler;

		WorldObjectRef world_ob = new WorldObject();
		world_ob->uid = UID(123);
		world_ob->model_url = "some cool model_url";

		WorldObjectRef world_ob2 = new WorldObject();
		world_ob2->uid = UID(124);

		AvatarRef avatar = new Avatar();
		avatar->name = "MrCool";
		avatar->uid = UID(456);

		ParcelRef parcel = new Parcel();
		parcel->id = ParcelID(789);

		main_world_state->getObjects(lock)[world_ob->uid] = world_ob;
		main_world_state->getObjects(lock)[world_ob2->uid] = world_ob2;
		main_world_state->getAvatars(lock)[avatar->uid] = avatar;
		
		//-------------------------------- Create a SubstrataLuaScript, don't execute though --------------------------------
		{
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "print('hello')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Create a SubstrataLuaScript, execute --------------------------------
		{
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "print('hello')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Test destroying a script that holds the only reference to its VM --------------------------------
		// ~LuaScript frees the script's Lua references using the LuaVM, so the VM must still be alive when ~LuaScript runs.
		// Run under ASan or a debug heap to detect the VM being used after it has been freed.
		{
			Reference<SubstrataLuaVM> heap_vm = new SubstrataLuaVM(SubstrataLuaVM::SubstrataLuaVMArgs(&server));
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(heap_vm, &output_handler, "print('hello')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			heap_vm = nullptr;
			testAssert(sub_lua_script->substrata_lua_vm->getRefCount() == 1); // The script now holds the only reference to the VM.

			sub_lua_script = nullptr; // Destroys the script, and with it the VM.
		}

		//-------------------------------- Test this_object --------------------------------
		{
			const std::string script_src = "print('this_object.uid: ' .. tostring(this_object.uid))  assert(this_object.uid == 123.0)\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "this_object.uid: 123");
		}
		testAssert(vm.getNumLuaValueReferences() == initial_num_lua_value_refs);

		//-------------------------------- Test IS_SERVER --------------------------------
		{
			const std::string script_src = "print('IS_SERVER: ' .. tostring(IS_SERVER))  assert(IS_SERVER == true)\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "IS_SERVER: true");
		}

		//-------------------------------- Test IS_CLIENT --------------------------------
		{
			const std::string script_src = "print('IS_CLIENT: ' .. tostring(IS_CLIENT))  assert(IS_CLIENT == false)\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "IS_CLIENT: false");
		}

		//-------------------------------- Test doOnUserTouchedObject --------------------------------
		testAssert(vm.getNumLuaValueReferences() == initial_num_lua_value_refs);
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserTouchedObject(world_ob->getOrCreateEventHandlers()->onUserTouchedObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 touched object 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}
		testAssert(vm.getNumLuaValueReferences() == initial_num_lua_value_refs);

		//-------------------------------- Test doOnUserUsedObject --------------------------------
		{
			const std::string script_src = 
				"function onUserUsedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' used object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserUsedObject_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserUsedObject(world_ob->getOrCreateEventHandlers()->onUserUsedObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 used object 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test doOnUserMovedNearToObject --------------------------------
		{
			const std::string script_src = 
				"function onUserMovedNearToObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' moved near to object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserMovedNearToObject(world_ob->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 moved near to object 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test doOnUserMovedAwayFromObject --------------------------------
		{
			const std::string script_src = 
				"function onUserMovedAwayFromObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' moved away from object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserMovedAwayFromObject(world_ob->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 moved away from object 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test doOnUserEnteredParcel --------------------------------
		{
			const std::string script_src = 
				"function onUserEnteredParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' entered parcel ' .. tostring(parcel.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserEnteredParcel(world_ob->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 entered parcel 789");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test doOnUserExitedParcel --------------------------------
		{
			const std::string script_src = 
				"function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' exited parcel ' .. tostring(parcel.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 exited parcel 789");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test onUserEnteredVehicle --------------------------------
		{
			const std::string script_src = 
				"function onUserEnteredVehicle(av : Avatar, vehicle_ob : Object)			\n"
				"    local vehicle = coroutine.wrap(function() return av.vehicle_inside end)()\n"
				"    assert(type(vehicle) == 'table' and vehicle.uid == vehicle_ob.uid)\n"
				"    assert(vehicle.model_url == vehicle_ob.model_url)\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' entered vehicle ' .. tostring(vehicle_ob.uid))			\n"
				"end \n"
				"addEventListener('onUserEnteredVehicle', 123, onUserEnteredVehicle)   \n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserEnteredVehicle_handlers.handler_funcs.size() == 1);
			const UID previous_vehicle_uid = avatar->vehicle_inside_uid;
			avatar->vehicle_inside_uid = world_ob->uid;
			sub_lua_script->doOnUserEnteredVehicle(world_ob->getOrCreateEventHandlers()->onUserEnteredVehicle_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			avatar->vehicle_inside_uid = previous_vehicle_uid;
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 entered vehicle 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test onUserExitedVehicle --------------------------------
		{
			const std::string script_src = 
				"function onUserExitedVehicle(av : Avatar, vehicle_ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' exited vehicle ' .. tostring(vehicle_ob.uid))			\n"
				"end \n"
				"addEventListener('onUserExitedVehicle', 123, onUserExitedVehicle)   \n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedVehicle_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserExitedVehicle(world_ob->getOrCreateEventHandlers()->onUserExitedVehicle_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "Avatar 456 exited vehicle 123");

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		//-------------------------------- Test doOnTimerEvent --------------------------------

		// The script creates a timer, then we call it.
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"createTimer(onTimerEvent, 0.1, false)	";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(sub_lua_script->timers[0].id == sub_lua_script->next_timer_id - 1);
			
			sub_lua_script->doOnTimerEvent(sub_lua_script->timers[0].onTimerEvent_ref, lock);

			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "onTimerEvent");
		}

		// The script creates a timer, then destroys it
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"local timer_handle = createTimer(onTimerEvent, 0.1, false)	\n"
				"destroyTimer(timer_handle)";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(sub_lua_script->timers[0].id == -1);

			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "");
		}

		// Test creating and destroying timers
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"local timers = {}						\n"
				"for i=1, 4 do  timers[i] = createTimer(onTimerEvent, 0.1, false)  end	\n"
				"for i=1, 4 do  destroyTimer(timers[i])  end	\n"
				"for i=1, 4 do  destroyTimer(timers[i])  end	\n" // Test destroying timers again, should have no effect.
				"destroyTimer(123)"; // Test destroying an invalid timer, should have no effect.

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			for(int i=0; i<4; ++i)
				testAssert(sub_lua_script->timers[i].id == -1);

			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "");
		}

		// -1 identifies a free slot internally. It must never be accepted as a Lua handle.
		{
			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "destroyTimer(-1)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}, "Invalid timer ID");
		}

		// Create eight timers across four slots. Returning a slot index repeats a handle in the second batch.
		{
			const std::string script_src =
				"local function callback() end\n"
				"local seen = {}\n"
				"for batch = 1, 2 do\n"
				"    local handles = {}\n"
				"    for i = 1, 4 do\n"
				"        local id = createTimer(callback, 0.1, false)\n"
				"        assert(not seen[id], 'timer handle reused')\n"
				"        seen[id] = true\n"
				"        handles[i] = id\n"
				"    end\n"
				"    for i = 4, 1, -1 do destroyTimer(handles[i]) end\n"
				"end";
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			for(int i=0; i<SubstrataLuaScript::MAX_NUM_TIMERS; ++i)
			{
				testAssert(sub_lua_script->timers[i].id == -1);
				testAssert(sub_lua_script->timers[i].onTimerEvent_ref.ref == LUA_NOREF);
			}
		}

		// A one-shot cancels itself and installs a replacement before the dispatcher cleans up the old timer.
		{
			const std::string script_src =
				"local first\n"
				"first = createTimer(function()\n"
				"    destroyTimer(first)\n"
				"    createTimer(function() print('replacement') end, 0.1, false)\n"
				"    destroyTimer(first)\n" // A stale Lua handle must leave the replacement alone.
				"end, 0.1, false)";

			server.timer_queue.clear();
			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			std::vector<ScriptTimerQueueTimer> triggered;
			server.timer_queue.update(server.total_timer.elapsed() + 1.0, triggered);
			testAssert(triggered.size() == 1);
			const ScriptTimerQueueTimer first = triggered[0];
			sub_lua_script->doOnTimerEvent(first.onTimerEvent_ref, lock);
			testAssert(!sub_lua_script->hit_error);
			sub_lua_script->destroyTimer(first.timer_index, first.timer_id); // Post-callback cleanup for the original one-shot.

			server.timer_queue.update(server.total_timer.elapsed() + 1.0, triggered);
			testAssert(triggered.size() == 1);
			const ScriptTimerQueueTimer& replacement = triggered[0];
			testAssert(replacement.timer_index == first.timer_index && replacement.timer_id != first.timer_id);
			testAssert(sub_lua_script->timers[replacement.timer_index].id == replacement.timer_id);
			sub_lua_script->doOnTimerEvent(replacement.onTimerEvent_ref, lock);
			testAssert(!sub_lua_script->hit_error && output_handler.buf == "replacement");
			sub_lua_script->destroyTimer(replacement.timer_index, replacement.timer_id);
			testAssert(sub_lua_script->timers[replacement.timer_index].onTimerEvent_ref.ref == LUA_NOREF);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		// The script creates a timer with a nil function, then we try and call it.  Check doesn't crash etc.
		{
			const std::string script_src =  
				"local timer_handle = createTimer(nil, 0.1, false)	";
			// Should get: createTimer(): arg 1 must be a function
			testThrowsExcepContainingString([&]() { 
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				},
				"arg 1 must be a function"
			);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		// Test creating too many timers
		{
			const std::string script_src =  
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"for i=1, 10 do  createTimer(onTimerEvent, 0.1, false)	end";
			// Should get: createTimer(): arg 1 must be a function
			testThrowsExcepContainingString([&]() { 
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				},
				"Could not create timer"
			);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		// Test creating a timer event then destroying the world object.  Test we don't do a dangling reference access.
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"createTimer(onTimerEvent, 0.1, false)	";

			WorldObjectRef temp_world_ob = new WorldObject();
			temp_world_ob->uid = UID(200);
			main_world_state->getObjects(lock)[temp_world_ob->uid] = temp_world_ob;

			output_handler.buf.clear();
			server.timer_queue.clear();

			temp_world_ob->sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, temp_world_ob.ptr(), &server, main_world_state.ptr(), lock);
			temp_world_ob->sub_lua_script->exec(lock);

			testAssert(temp_world_ob->sub_lua_script->timers[0].id == temp_world_ob->sub_lua_script->next_timer_id - 1);
			
			// The timer event should have got added to the server timer queue.  Dequeue it.
			std::vector<ScriptTimerQueueTimer> triggered_timers;
			server.timer_queue.update(/*cur time=*/server.total_timer.elapsed() + 1.0, triggered_timers);
			testAssert(triggered_timers.size() == 1);

			testAssert(triggered_timers[0].sub_lua_script.getPtrIfAlive() == temp_world_ob->sub_lua_script.ptr());

			// Delete the ob
			main_world_state->getObjects(lock).erase(temp_world_ob->uid);
			temp_world_ob = nullptr;

			// Test the weak reference notices that the object and its sub_lua_script has been destroyed
			testAssert(triggered_timers[0].sub_lua_script.getPtrIfAlive() == nullptr);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		// Test adding a timer inside a timer event handler 
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)								\n"
				"		print('onTimerEvent')									\n"
				"		createTimer(onTimerEvent, 0.1, false)					\n" // Create another timer!
				"end															\n"
				"createTimer(onTimerEvent, 0.1, false)							\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(sub_lua_script->timers[0].id == sub_lua_script->next_timer_id - 1);

			sub_lua_script->doOnTimerEvent(sub_lua_script->timers[0].onTimerEvent_ref, lock);

			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "onTimerEvent");
		}

		// Test destroying the timer inside the timer event handler for it
		{
			const std::string script_src = 
				"local timer_handle = nil										\n"
				"function onTimerEvent(ob : Object)								\n"
				"		print('onTimerEvent')									\n"
				"		destroyTimer(timer_handle)								\n" // Destroy the timer!
				"end															\n"
				"timer_handle = createTimer(onTimerEvent, 0.1, false)			\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(sub_lua_script->timers[0].id == sub_lua_script->next_timer_id - 1);

			sub_lua_script->doOnTimerEvent(sub_lua_script->timers[0].onTimerEvent_ref, lock);

			testAssert(!sub_lua_script->hit_error);
			testAssert(output_handler.buf == "onTimerEvent");
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);


		//-------------------------------- Test getObjectForUID --------------------------------
		{ // Test successful getObjectForUID call
			const std::string script_src = "local ob = getObjectForUID(123)";
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		{ // Look up a different object from a coroutine and use the returned handle on the root thread.
			const std::string script_src =
				"local ob = coroutine.wrap(function() return getObjectForUID(124) end)()\n"
				"assert(type(ob) == 'table' and ob.uid == 124)\n"
				"assert(getmetatable(ob) == getmetatable(this_object))";
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		{ // Test unsuccessful getObjectForUID call
			const std::string script_src = "local ob = getObjectForUID(6544234)";
			testThrowsExcepContainingString([&]() { 
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock); 
				}, 
				"No object with UID 6544234"
			);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Test getMaterial --------------------------------
		{
			WorldMaterialRef mat0 = new WorldMaterial();
			WorldMaterialRef mat1 = new WorldMaterial();
			mat1->roughness.val = 0.25f;
			mat1->opacity.val = 0.5f;
			world_ob->materials = { mat0, mat1 };

			{ // Read fields of the second material
				const std::string script_src =
					"local mat = this_object:getMaterial(1)\n"
					"assert(mat.roughness_val == 0.25)\n"
					"assert(mat.opacity_val == 0.5)";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}
			{ // Index == number of materials
				testThrowsExcepContainingString([&]() {
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "this_object:getMaterial(2)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				}, "Invalid material index");
			}
			{ // Negative index
				testThrowsExcepContainingString([&]() {
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "this_object:getMaterial(-1)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				}, "Invalid material index");
			}
			{ // NaN index
				testThrowsExcepContainingString([&]() {
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "this_object:getMaterial(0/0)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				}, "Invalid material index");
			}

			world_ob->materials.clear(); // Clean up from test
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Test setting string fields (assignStringWithSizeCheck) --------------------------------
		{
			const URLString prev_model_url = world_ob->model_url;
			const std::string prev_content = world_ob->content;
			world_ob->materials = { new WorldMaterial() };
			auto run =[&](const std::string& src) { Reference<SubstrataLuaScript> s = new SubstrataLuaScript(&vm, &output_handler, src, world_ob.ptr(), &server, main_world_state.ptr(), lock); s->exec(lock); };

			run("this_object.model_url = 'model.glb'   this_object.content = 42   this_object:getMaterial(0).colour_texture_url = 'tex.png'");
			testAssert(world_ob->model_url == "model.glb");
			testAssert(world_ob->content == "42"); // Number is converted to a string
			testAssert(world_ob->materials[0]->colour_texture_url == "tex.png");

			testThrowsExcepContainingString([&]() { run("this_object.model_url = string.rep('a', 1001)"); }, "length too long");
			testThrowsExcepContainingString([&]() { run("this_object.content = {}"); }, "Value was not a string");

			world_ob->materials.clear(); // Clean up from test
			world_ob->model_url = prev_model_url;
			world_ob->content = prev_content;
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Test getCurrentTime --------------------------------
		{
			const std::string script_src = "getCurrentTime()";
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}

		//-------------------------------- Test showMessageToUser --------------------------------
		{
			const std::string script_src = "local avatar = {uid=456}   showMessageToUser('hello thar', avatar)";
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		{ // Test invalid showMessageToUser call (avatar has no uid field)
			const std::string script_src = "local avatar = {}   showMessageToUser('hello thar', avatar)";
			testExceptionExpected([&]() { 
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock); 
			});

		}
		{ // Test invalid showMessageToUser call (avatar not a table)
			const std::string script_src = "local avatar = 123.0   showMessageToUser('hello thar', avatar)";
			testExceptionExpected([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			});
		}

		//-------------------------------- Test showMessage --------------------------------
		{ // Does nothing on the server, but shouldn't throw.
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "showMessage('hello thar')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		{ // No args
			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "showMessage()", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}, "Expected 1 arg(s)");
		}
		{ // Message not a string
			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "showMessage({})", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}, "Value was not a string");
		}

		//-------------------------------- Test number used where a string key or event name is expected --------------------------------
		{
			const char* scripts[] = {
				"local x = this_object[1]",
				"this_object[1] = 2",
				"addEventListener(1, 123, function() end)"
			};
			for(const char* script_src : scripts)
			{
				testThrowsExcepContainingString([&]() {
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				}, "Value was not a string");
			}
		}

		//-------------------------------- Test getSecret --------------------------------
		{
			const UserID prev_creator_id = world_ob->creator_id;
			world_ob->creator_id = UserID(10);

			UserSecretRef secret = new UserSecret();
			secret->key = UserSecretKey(UserID(10), "api_key");
			secret->value = "abc";
			server.world_state->user_secrets[secret->key] = secret;

			UserSecretRef other_user_secret = new UserSecret(); // Secret belonging to a different user
			other_user_secret->key = UserSecretKey(UserID(11), "other_key");
			other_user_secret->value = "def";
			server.world_state->user_secrets[other_user_secret->key] = other_user_secret;

			{
				const std::string script_src =
					"assert(getSecret('api_key') == 'abc')\n"
					"assert(getSecret('missing_key') == nil)\n"
					"assert(getSecret('other_key') == nil)"; // Can't read secrets of a user other than the object creator.
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}
			{ // Secret name not a string
				testExceptionExpected([&]() {
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "getSecret({})", world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				});
			}

			server.world_state->user_secrets.clear(); // Clean up from test
			world_ob->creator_id = prev_creator_id;
		}

		//-------------------------------- Test addEventListener --------------------------------
		// Test a listener function added both implicitly (to object 123) and explicitly (to object 124)
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', 124, onUserTouchedObject)			";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.nonEmpty());
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

			world_ob ->event_handlers = NULL; // Clean up from test
			world_ob2->event_handlers = NULL; // Clean up from test
		}

		// Test listener added twice with addEventListener
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', 124, onUserTouchedObject)			\n"
				"addEventListener('onUserTouchedObject', 124, onUserTouchedObject)			\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

			world_ob ->event_handlers = NULL; // Clean up from test
			world_ob2->event_handlers = NULL; // Clean up from test
		}

		// Test explicit addEventListener call for an event we are already implicitly listening to.  Should just end up with one event listener
		{
			world_ob->event_handlers = NULL;

			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', 123, onUserTouchedObject)			\n"; // Try and add the same listener to the same object

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 123"));

			world_ob->event_handlers = NULL; // Clean up from test
		}

		// Test addEventListener call with an anonymous function as the listener function
		{
			world_ob->event_handlers = NULL;

			const std::string script_src = 
				"addEventListener('onUserTouchedObject', 123,													\n"
				"	function(av : Avatar, ob : Object)															\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"	end																							\n"
				")																								\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 123"));

			world_ob->event_handlers = NULL; // Clean up from test
		}

		// Test addEventListener call in the handler for the event!
		{
			world_ob->event_handlers = NULL;

			const std::string script_src = 
				"function onUserTouchedObject2(av : Avatar, ob : Object)										\n"
				"end																							\n"
				"function onUserTouchedObject(av : Avatar, ob : Object)											\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"		addEventListener('onUserTouchedObject', 123, onUserTouchedObject2)						\n"  // uhoh
				"end																							\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 123"));

			world_ob->event_handlers = NULL; // Clean up from test
		}

		// Test addEventListener call with an anonymous function in the handler for the event!
		{
			world_ob->event_handlers = NULL;

			const std::string script_src =
				"function onUserTouchedObject(av : Avatar, ob : Object)												\n"
				"	print('in onUserTouchedObject')																	\n"
				"	addEventListener('onUserTouchedObject', 123,													\n"
				"		function(av : Avatar, ob : Object)															\n"
				"			print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"			onUserTouchedObject(av, ob)																\n"
				"		end																							\n"
				"	)																								\n"
				"end																								\n";

			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == sub_lua_script.ptr());

			// Execute the event handler
			world_ob->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob->uid, lock);

			//testEqual(output_handler.buf, std::string("Avatar 456 touched object 123"));

			world_ob->event_handlers = NULL; // Clean up from test
		}


		// Test invalid addEventListener call - invalid event
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"addEventListener('notAnEvent', 124, onUserTouchedObject)			\n";

			testThrowsExcepContainingString([&]() { 
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock); 
				}, 
				"Unknown event"
			);
		}

		// Test invalid addEventListener call - no such object with UID
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"addEventListener('notAnEvent', 124111111, onUserTouchedObject)			\n";

			testThrowsExcepContainingString([&]() { 
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock); 
				}, 
				"No object"
			);
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		// Test invalid addEventListener calls - trying to add too many event listeners
		// NOTE: test disabled since adding multiple of the same listener doesn't increment listener count now.
		/*{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"for i=1, 10000 do addEventListener('onUserTouchedObject', 124, onUserTouchedObject)		end	\n";

			testThrowsExcepContainingString([&]() { new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock); }, "too many event listeners");
		}*/

		// Test addEventListener called from a script that is then destroyed.
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', 124, onUserTouchedObject)			";

			output_handler.buf.clear();
			world_ob2->event_handlers = NULL; // Clean up from prior tests

			WorldObjectRef temp_world_ob = new WorldObject();
			temp_world_ob->uid = UID(200);
			main_world_state->getObjects(lock)[temp_world_ob->uid] = temp_world_ob;

			temp_world_ob->sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, temp_world_ob.ptr(), &server, main_world_state.ptr(), lock);
			temp_world_ob->sub_lua_script->exec(lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == temp_world_ob->sub_lua_script.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

			// Delete the ob
			main_world_state->getObjects(lock).erase(temp_world_ob->uid);
			temp_world_ob = nullptr;

			// Try and execute the event handler again.  This time the handler should be removed as the referenced object is dead.
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 0); // Handler should have been removed.

			world_ob2->event_handlers = NULL; // Clean up from test
		}
		testEqual(vm.getNumLuaValueReferences(), initial_num_lua_value_refs);

		//-------------------------------- Test objectstorage.setItem  --------------------------------
		{
			const std::string script_src = "objectstorage.setItem(\"a\", 1230.0)";

			server.world_state->object_storage_items.clear();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(server.world_state->object_storage_items.size() == 1);
			const ObjectStorageKey key(world_ob->uid, "a");
			testAssert(server.world_state->object_storage_items.count(key) == 1);
			testAssert(server.world_state->object_storage_items[key]->key == key);
			testAssert(server.world_state->object_storage_items[key]->data.size() == 4 + 1 + 8);
		}

		// Test calling objectstorage.setItem multiple times with same key
		{
			const std::string script_src = "for i=1,10 do objectstorage.setItem(\"a\", i) end";

			server.world_state->object_storage_items.clear();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(server.world_state->object_storage_items.size() == 1);
			const ObjectStorageKey key(world_ob->uid, "a");
			testAssert(server.world_state->object_storage_items.count(key) == 1);
			testAssert(server.world_state->object_storage_items[key]->key == key);
			testAssert(server.world_state->object_storage_items[key]->data.size() == 4 + 1 + 8);
		}
		
		// Test calling objectstorage.setItem multiple times with a different key each time
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "for i=1,2000 do objectstorage.setItem(tostring(i), i) end";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Too many object storage items for object");
		}

		// Test calling objectstorage.setItem multiple times with a different key each time, one event handler at a time.
		{
			const std::string script_src = 
				"local i = 1																	\n"
				"function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"		--print(tostring(i))													\n"
				"		objectstorage.setItem(tostring(i), i)									\n"
				"		i += 1																	\n"
				"end";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);

			for(int i=0; i<2000; ++i)
				sub_lua_script->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(sub_lua_script->hit_error);

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		// Test calling objectstorage.setItem with a non-string key
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.setItem({a=1.0}, 1000.0)";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Argument 1 was not a string");
		}

		// Test with a key that is too long
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.setItem('" + std::string(1000, 'a') + "', 1000.0)";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Key is too long");
		}
		
		// Test with a value that is too long
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = 
					"local t = {}    \n"
					"for i=1,6000 do   \n" // Need to make the table sufficiently large to exceed max_serialised_size_B without triggering max_num_interrupts :)
					"	t[i] = i		\n"
					"end				\n"
					"objectstorage.setItem('a', t)";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Serialised data exceeded max_serialised_size_B");
		}

		//-------------------------------- Test objectstorage.getItem  --------------------------------
		{
			const std::string script_src = 
				"objectstorage.setItem(\"a\", 16.0)				\n"
				"local x = objectstorage.getItem(\"a\")			\n"
				"assert(x == 16.0)								\n";

			server.world_state->clearObjectStorageItems();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(server.world_state->object_storage_items.size() == 1);
			const ObjectStorageKey key(world_ob->uid, "a");
			testAssert(server.world_state->object_storage_items.count(key) == 1);
			testAssert(server.world_state->object_storage_items[key]->key == key);
			testAssert(server.world_state->object_storage_items[key]->data.size() == 4 + 1 + 8);
		}

		// Test serialising and deserialising a plain old table
		{
			const std::string script_src = 
				"objectstorage.setItem(\"a\", { b = 16.0 })		\n"
				"local x = objectstorage.getItem(\"a\")			\n"
				"assert(x.b == 16.0)							\n";

			server.world_state->clearObjectStorageItems();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}

		// Test serialising and deserialising a Vec3d
		{
			const std::string script_src = 
				"objectstorage.setItem(\"a\", Vec3d(1.0, 2.0, 3.0))				\n"
				"local v = objectstorage.getItem(\"a\")							\n"
				"assert(v == Vec3d(1.0, 2.0, 3.0))								\n"
				"v2 = v + v														\n" // Test metatable is correct and that __add is called.
				"assert(v2 == Vec3d(2.0, 4.0, 6.0))								\n";

			server.world_state->clearObjectStorageItems();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(server.world_state->object_storage_items.size() == 1);
			const ObjectStorageKey key(world_ob->uid, "a");
			testAssert(server.world_state->object_storage_items.count(key) == 1);
			testAssert(server.world_state->object_storage_items[key]->key == key);
		}

		// Test serialising and deserialising an Avatar
		{
			const std::string script_src = 
				"function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"	objectstorage.setItem(\"a\", av)					\n"
				"	local x = objectstorage.getItem(\"a\")				\n"
				"	assert(x.uid == 456)								\n"
				"	assert(x.name == 'MrCool')							\n"
				"end";

			world_ob->event_handlers = NULL;
			server.world_state->clearObjectStorageItems();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!sub_lua_script->hit_error);

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		// Test serialising and deserialising an object
		{
			const std::string script_src = 
				"function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"	objectstorage.setItem(\"a\", ob)					\n"
				"	local x = objectstorage.getItem(\"a\")				\n"
				"	assert(x.uid == 123)								\n"
				"	assert(x.model_url == 'some cool model_url')		\n"
				"end";

			world_ob->event_handlers = NULL;
			server.world_state->clearObjectStorageItems();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			sub_lua_script->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!sub_lua_script->hit_error);

			world_ob->event_handlers = nullptr; // Free ObjectEventHandlers, which frees references to Lua event handler functions.
		}

		// Test calling objectstorage.getItem with a key that is not present returns nil
		{
			const std::string script_src = 
				"local x = objectstorage.getItem(\"a\")			\n"
				"assert(x == nil)								\n";

			server.world_state->clearObjectStorageItems();

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testAssert(server.world_state->object_storage_items.size() == 0); // Check didn't add item
		}

		// Test calling objectstorage.getItem with a non-string key
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.getItem({a=1.0})";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Argument 1 was not a string");
		}

		// Test items persist across script instances, overwriting returns the latest value, and items are per-object.
		{
			server.world_state->clearObjectStorageItems();
			{
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "objectstorage.setItem('s', 'first')  objectstorage.setItem('s', 'second')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}
			testAssert(server.world_state->object_storage_items.size() == 1);
			{ // A new script on the same object reads the stored value.
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "assert(objectstorage.getItem('s') == 'second')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}
			{ // A script on a different object can't see it.
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "assert(objectstorage.getItem('s') == nil)", world_ob2.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}
			server.world_state->clearObjectStorageItems();
		}

		// Test key length limit: 256 chars is OK, 257 is too long.
		{
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "objectstorage.setItem('" + std::string(256, 'a') + "', 1.0)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);

			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script2 = new SubstrataLuaScript(&vm, &output_handler, "objectstorage.setItem('" + std::string(257, 'a') + "', 1.0)", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script2->exec(lock);
			}, "Key is too long");
			server.world_state->clearObjectStorageItems();
		}

		// Test missing args
		{
			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "objectstorage.setItem('a')", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}, "Expected 2 arg(s)");
			testThrowsExcepContainingString([&]() {
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, "objectstorage.getItem()", world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			}, "Expected 1 arg(s)");
		}

		//-------------------------------- Test parseJSON  --------------------------------
		{
			const std::string script_src = 
				"local x = parseJSON('123.456')						\n"
				"assert(x == 123.456)								\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting an array
		{
			const std::string script_src = 
				"local x = parseJSON('[123.456, true, \"abc\", null]')			\n"
				"print(x)														\n"
				"print(x[1])													\n"
				"assert(x[1] == 123.456)										\n"
				"assert(x[2] == true)											\n"
				"assert(x[3] == 'abc')											\n"
				"assert(x[4] == nil)											\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting an object
		{
			const std::string script_src = 
				"local x = parseJSON('{ \"a\" : 123.456, \"b\" : true, \"c\" : \"abc\", \"d\" : null}')			\n"
				"print(x)														\n"
				"print(x[1])													\n"
				"assert(x['a'] == 123.456)										\n"
				"assert(x['b'] == true)											\n"
				"assert(x['c'] == 'abc')										\n"
				"assert(x['d'] == nil)											\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting a nested object
		{
			const std::string script_src = 
				"local x = parseJSON('{ \"a\" : { \"b\" : { \"c\" : 100.0 } } }')	\n"
				"local a = x.a													\n"
				"local b = a.b													\n"
				"local c = b.c													\n"
				"print(x)														\n"
				"print(x[1])													\n"
				"assert(c == 100.0)												\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting an empty object
		{
			const std::string script_src = 
				"local x = parseJSON('{}')										\n"
				"print(x)														\n"
				"assert(#x == 0)												\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting an empty array
		{
			const std::string script_src = 
				"local x = parseJSON('[]')										\n"
				"print(x)														\n"
				"assert(#x == 0)												\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting just null
		{
			const std::string script_src = 
				"local x = parseJSON('null')									\n"
				"assert(x == nil)												\n";

			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test converting a deeply nested object
		{
			std::string script_src = "local x = parseJSON('";
			for(int i=0; i<100; ++i)
				script_src += std::string("{ \"a\" : ");
			script_src += "{}" + std::string(100, '}') + "')						\n"
				"												\n";
			//conPrint(script_src);
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}
		// Test converting a deeply nested array
		{
			std::string script_src = "local x = parseJSON('" + 
				std::string(100, '[') + "[]" + std::string(100, ']') + "')						\n";
			//conPrint(script_src);
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testAssert(!sub_lua_script->hit_error);
		}

		// Test some invalid json
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON(' ')    ";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON('[[[')    ";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON('truAAA')    ";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON(true)    ";
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
			},
			"was not a string"
		);


		//-------------------------------- Test HTTP callback dispatch and reference lifetimes --------------------------------
		{
			// Inject results directly: exercise the real completion path without HTTP servers, network access, or timing dependencies.
			LuaHTTPRequestManager http_manager(&server);
			enum HTTPTestCase { Success, Error, RateLimited, SuccessCallbackThrows, ErrorCallbackThrows, ScriptAlreadyErrored, ScriptDestroyed };
			for(int test_case = Success; test_case <= ScriptDestroyed; ++test_case)
			{
				const int refs_before_script = vm.getNumLuaValueReferences();
				const bool error_result = test_case == Error || test_case == RateLimited || test_case == ErrorCallbackThrows;
				const int error_code = test_case == RateLimited ? LuaHTTPRequestResult::ErrorCode_RateLimited : LuaHTTPRequestResult::ErrorCode_Other;
				const std::string script_src =
					"function httpDone(response)\n"
					"    assert(response.response_code == 201)\n"
					"    assert(response.response_message == 'Created')\n"
					"    assert(response.mime_type == 'application/octet-stream')\n"
					"    assert(buffer.len(response.body_data) == 3)\n"
					"    assert(buffer.readu8(response.body_data, 0) == 65)\n"
					"    assert(buffer.readu8(response.body_data, 1) == 0)\n"
					"    assert(buffer.readu8(response.body_data, 2) == 255)\n"
					"    print('done')\n" +
					std::string(test_case == SuccessCallbackThrows ? "    error('HTTP callback failed')\n" : "") +
					"end\n"
					"function httpError(response)\n"
					"    assert(response.error_code == " + toString(error_code) + ")\n"
					"    assert(response.error_description == 'test HTTP error')\n"
					"    print('error')\n" +
					std::string(test_case == ErrorCallbackThrows ? "    error('HTTP callback failed')\n" : "") +
					"end\n";
				output_handler.buf.clear();
				Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				sub_lua_script->exec(lock);
				const int refs_with_script = vm.getNumLuaValueReferences();
				Reference<LuaHTTPRequest> request = new LuaHTTPRequest();
				request->sub_lua_script = sub_lua_script;
				request->onDone_ref = sub_lua_script->getRefToFunction("httpDone").ref;
				request->onError_ref = sub_lua_script->getRefToFunction("httpError").ref;
				testEqual(vm.getNumLuaValueReferences(), refs_with_script + 2);

				Reference<LuaHTTPRequestResult> result = new LuaHTTPRequestResult();
				result->request = request;
				result->response.response_code = 201;
				result->response.response_message = "Created";
				result->response.mime_type = "application/octet-stream";
				result->data.push_back(65);
				result->data.push_back(0);
				result->data.push_back(255);
				if(error_result)
				{
					result->error_code = error_code;
					result->exception_msg = "test HTTP error";
				}
				http_manager.enqueueResult(result);

				if(test_case == ScriptAlreadyErrored)
					sub_lua_script->hit_error = true;
				if(test_case == ScriptDestroyed)
				{
					sub_lua_script = nullptr;
					testEqual(vm.getNumLuaValueReferences(), refs_before_script);
				}

				http_manager.think();
				if(test_case == ScriptAlreadyErrored || test_case == ScriptDestroyed)
					testAssert(output_handler.buf.empty());
				else if(test_case == SuccessCallbackThrows || test_case == ErrorCallbackThrows)
				{
					testAssert(sub_lua_script->hit_error);
					testAssert(output_handler.buf.find("HTTP callback failed") != std::string::npos);
				}
				else
				{
					testAssert(!sub_lua_script->hit_error);
					testEqual(output_handler.buf, std::string(error_result ? "error" : "done"));
				}
				// Both callbacks (including the unused one) must be released while the script is still alive.
				testEqual(vm.getNumLuaValueReferences(), test_case == ScriptDestroyed ? refs_before_script : refs_with_script);
				const std::string output_after_completion = output_handler.buf;
				http_manager.think();
				testEqual(output_handler.buf, output_after_completion);
				sub_lua_script = nullptr;
				testEqual(vm.getNumLuaValueReferences(), refs_before_script);
			}
		}

		//-------------------------------- Test Lua HTTP calls with HTTP disabled --------------------------------
		{
			const auto previous_flags = server.world_state->feature_flag_info.feature_flags;
			server.world_state->feature_flag_info.feature_flags &= ~ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG;
			server.lua_http_manager = new LuaHTTPRequestManager(&server);
			for(int use_post = 0; use_post < 2; ++use_post)
			{
				for(int use_coroutine = 0; use_coroutine < 2; ++use_coroutine)
				{
					const int refs_before_script = vm.getNumLuaValueReferences();
					const std::string call = use_post ?
						"doHTTPPostRequestAsync('http://example.invalid/test', 'payload', 'text/plain', {['X-Test'] = 'value'}, done, failed)\n" :
						"doHTTPGetRequestAsync('http://example.invalid/test', {['X-Test'] = 'value'}, done, failed)\n";
					const std::string script_src =
						"local function done(response) print('Unexpected success callback') end\n"
						"local function failed(response) print('Unexpected error callback') end\n" +
						(use_coroutine ? "local co = coroutine.create(function()\n" + call + "end)\nlocal ok, err = coroutine.resume(co)\nassert(ok, err)\n" : call);
					output_handler.buf.clear();
					bool caught_disabled_exception = false;
					try
					{
						Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
						sub_lua_script->exec(lock);
					}
					catch(const glare::Exception& e)
					{
						testAssert(std::string(e.what()).find("Lua HTTP requests feature-flag disabled") != std::string::npos);
						caught_disabled_exception = true;
					}
					testAssert(caught_disabled_exception);
					// Construction failed: neither the script thread nor callback references should remain.
					testEqual(vm.getNumLuaValueReferences(), refs_before_script);
					server.lua_http_manager->think();
					testAssert(output_handler.buf.empty());
					testEqual(vm.getNumLuaValueReferences(), refs_before_script);
				}
			}
			server.lua_http_manager = nullptr;
			server.world_state->feature_flag_info.feature_flags = previous_flags;
		}

		//-------------------------------- Test Lua HTTP calls with a number key in the headers table --------------------------------
		// {'boo'} has the number key 1.  Header table keys must be strings, so check this gives a script error, without leaking references.
		{
			server.lua_http_manager = new LuaHTTPRequestManager(&server);
			for(int use_post = 0; use_post < 2; ++use_post)
			{
				const int refs_before_script = vm.getNumLuaValueReferences();
				const std::string script_src = std::string("local function done(response) end\nlocal function failed(response) end\n") + (use_post ?
					"doHTTPPostRequestAsync('http://example.invalid/test', 'payload', 'text/plain', {'boo'}, done, failed)\n" :
					"doHTTPGetRequestAsync('http://example.invalid/test', {'boo'}, done, failed)\n");
				bool caught_exception = false;
				try
				{
					Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
					sub_lua_script->exec(lock);
				}
				catch(const glare::Exception& e)
				{
					testAssert(std::string(e.what()).find("additional header table keys must be strings") != std::string::npos);
					caught_exception = true;
				}
				testAssert(caught_exception);
				testEqual(vm.getNumLuaValueReferences(), refs_before_script);
			}
			server.lua_http_manager = nullptr;
		}

		//-------------------------------- Test a real Lua HTTP GET request --------------------------------
		{
			server.lua_http_manager = new LuaHTTPRequestManager(&server);

			const int refs_before_script = vm.getNumLuaValueReferences();
			const std::string script_src =
				"local function done(response)\n"
				"    assert(response.response_code == 200, 'Unexpected HTTP status: ' .. tostring(response.response_code))\n"
				"    assert(string.find(response.mime_type, 'text/html', 1, true), 'Expected HTML response')\n"
				"    assert(buffer.len(response.body_data) > 0, 'Empty HTTP response body')\n"
				"    print(buffer.tostring(response.body_data))\n"
				"    print('HTTP download complete')\n"
				"end\n"
				"local function failed(response)\n"
				"    error('HTTP download failed: ' .. response.error_description)\n"
				"end\n"
				"doHTTPGetRequestAsync('https://forwardscattering.org/post/3', {}, done, failed)\n";
			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 3); // Thread and two pending callbacks.

			Timer request_timer;
			while(output_handler.buf.empty() && !sub_lua_script->hit_error && request_timer.elapsed() < 30.0)
			{
				server.lua_http_manager->think();
				if(output_handler.buf.empty() && !sub_lua_script->hit_error)
					PlatformUtils::Sleep(10);
			}

			// Stop the workers (including any timed-out download) before reporting a test failure.
			server.lua_http_manager = nullptr;
			if(output_handler.buf.empty() && !sub_lua_script->hit_error)
				failTest("Timed out waiting for Lua HTTP GET https://forwardscattering.org/post/3");
			testAssert(!sub_lua_script->hit_error);
			testAssert(::hasPrefix(output_handler.buf, "<!DOCTYPE html>"));
			testAssert(output_handler.buf.find("<html") != std::string::npos);
			testAssert(output_handler.buf.find("</html>") != std::string::npos);
			const std::string completion_message = "HTTP download complete";
			testAssert(output_handler.buf.size() >= completion_message.size());
			testEqual(output_handler.buf.substr(output_handler.buf.size() - completion_message.size()), completion_message);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 1); // Both callback references were released.
			sub_lua_script = nullptr;
			testEqual(vm.getNumLuaValueReferences(), refs_before_script);
		}

		//-------------------------------- Test rate limiting with real Lua HTTP requests --------------------------------
		{
			server.lua_http_manager = new LuaHTTPRequestManager(&server); // Fresh per-user rate-limit allowance.
			const int refs_before_script = vm.getNumLuaValueReferences();
			const std::string script_src =
				"local completed, accepted, rate_limited = 0, 0, 0\n"
				"local function finished()\n"
				"    completed += 1\n"
				"    assert(completed <= 11, 'Duplicate HTTP callback')\n"
				"    if completed == 11 then\n"
				"        assert(accepted == 10)\n"
				"        assert(rate_limited == 1)\n"
				"        print('Rate-limit test complete')\n"
				"    end\n"
				"end\n"
				"for i = 1, 11 do\n"
				"    local request_index = i\n"
				"    local function done(response)\n"
				"        assert(request_index <= 10, 'Eleventh request was accepted')\n"
				"        accepted += 1\n"
				"        finished()\n"
				"    end\n"
				"    local function failed(response)\n"
				"        if response.error_code == " + toString((int)LuaHTTPRequestResult::ErrorCode_RateLimited) + " then\n"
				"            assert(request_index == 11, 'Request rejected before allowance was exhausted')\n"
				"            rate_limited += 1\n"
				"            print('Rate limited')\n"
				"        else\n"
				"            assert(request_index <= 10, 'Eleventh request reached the network')\n"
				"            accepted += 1\n"
				"            print('Network error: ' .. response.error_description)\n"
				"        end\n"
				"        finished()\n"
				"    end\n"
				"    doHTTPGetRequestAsync('https://forwardscattering.org/post/3', {}, done, failed)\n"
				"end\n";
			output_handler.buf.clear();
			Timer request_timer;
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, script_src, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 23); // Thread plus two references per request.

			const std::string completion_message = "Rate-limit test complete";
			while(output_handler.buf.find(completion_message) == std::string::npos && !sub_lua_script->hit_error && request_timer.elapsed() < 30.0)
			{
				server.lua_http_manager->think();
				if(output_handler.buf.find("Rate limited") != std::string::npos)
					testAssert(request_timer.elapsed() >= 2.0); // Rejection must be delivered through the delayed queue.
				if(output_handler.buf.find(completion_message) == std::string::npos && !sub_lua_script->hit_error)
					PlatformUtils::Sleep(10);
			}

			server.lua_http_manager = nullptr;
			testAssert(!sub_lua_script->hit_error);
			if(output_handler.buf.find(completion_message) == std::string::npos)
				failTest("Timed out waiting for the 11 Lua HTTP request callbacks");
			testAssert(output_handler.buf.find("Rate limited") != std::string::npos);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 1);
			sub_lua_script = nullptr;
			testEqual(vm.getNumLuaValueReferences(), refs_before_script);
		}

		//-------------------------------- Test the delayed HTTP result queue cap --------------------------------
		{
			server.lua_http_manager = new LuaHTTPRequestManager(&server);
			server.lua_http_manager->max_requests_per_period = 1;
			server.lua_http_manager->max_delayed_results = 1;
			const int refs_before_script = vm.getNumLuaValueReferences();
			const std::string callbacks =
				"function done(response) print('network') end\n"
				"function failed(response)\n"
				"    if response.error_code == " + toString((int)LuaHTTPRequestResult::ErrorCode_RateLimited) + " then\n"
				"        print('limited')\n"
				"    else\n"
				"        print('network')\n"
				"    end\n"
				"end\n";
			const std::string call = "doHTTPGetRequestAsync('https://forwardscattering.org/post/3', {}, done, failed)\n";
			output_handler.buf.clear();
			Reference<SubstrataLuaScript> sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, callbacks + call + call, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			const int refs_with_pending_requests = vm.getNumLuaValueReferences();
			testEqual(refs_with_pending_requests, refs_before_script + 5); // Thread, one accepted request, and one delayed result.

			// Keep the script alive while rejecting a request, so destructor cleanup cannot hide leaked callback refs.
			Reference<LuaHTTPRequest> rejected_request = new LuaHTTPRequest();
			rejected_request->script_user_id = world_ob->creator_id;
			rejected_request->sub_lua_script = sub_lua_script;
			rejected_request->request_type = "GET";
			rejected_request->URL = "https://forwardscattering.org/post/3";
			rejected_request->onDone_ref = sub_lua_script->getRefToFunction("done").ref;
			rejected_request->onError_ref = sub_lua_script->getRefToFunction("failed").ref;
			testEqual(vm.getNumLuaValueReferences(), refs_with_pending_requests + 2);
			bool caught_queue_cap = false;
			try
			{
				server.lua_http_manager->enqueueHTTPRequest(rejected_request);
			}
			catch(const glare::Exception& e)
			{
				testAssert(std::string(e.what()).find("Too many rate-limited HTTP requests") != std::string::npos);
				caught_queue_cap = true;
			}
			testAssert(caught_queue_cap);
			testEqual(vm.getNumLuaValueReferences(), refs_with_pending_requests);

			// Also reject during top-level execution, before a Reference owns the newly constructed evaluator.
			caught_queue_cap = false;
			try
			{
				Reference<SubstrataLuaScript> rejected_evaluator = new SubstrataLuaScript(&vm, &output_handler, callbacks + call, world_ob.ptr(), &server, main_world_state.ptr(), lock);
				rejected_evaluator->exec(lock);
			}
			catch(const glare::Exception& e)
			{
				testAssert(std::string(e.what()).find("Too many rate-limited HTTP requests") != std::string::npos);
				caught_queue_cap = true;
			}
			testAssert(caught_queue_cap);
			testEqual(vm.getNumLuaValueReferences(), refs_with_pending_requests);

			Timer request_timer;
			while(vm.getNumLuaValueReferences() != refs_before_script + 1 && !sub_lua_script->hit_error && request_timer.elapsed() < 30.0)
			{
				server.lua_http_manager->think();
				PlatformUtils::Sleep(10);
			}
			testAssert(!sub_lua_script->hit_error);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 1);
			testAssert(output_handler.buf == "networklimited" || output_handler.buf == "limitednetwork");
			sub_lua_script = nullptr;
			testEqual(vm.getNumLuaValueReferences(), refs_before_script);

			// Draining the delayed result frees the queue slot, even though this user's rate allowance is still exhausted.
			output_handler.buf.clear();
			sub_lua_script = new SubstrataLuaScript(&vm, &output_handler, callbacks + call, world_ob.ptr(), &server, main_world_state.ptr(), lock);
			sub_lua_script->exec(lock);
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 3);
			request_timer.reset();
			while(output_handler.buf.empty() && !sub_lua_script->hit_error && request_timer.elapsed() < 10.0)
			{
				server.lua_http_manager->think();
				PlatformUtils::Sleep(10);
			}
			server.lua_http_manager = nullptr;
			testAssert(!sub_lua_script->hit_error);
			testEqual(output_handler.buf, std::string("limited"));
			testEqual(vm.getNumLuaValueReferences(), refs_before_script + 1);
			sub_lua_script = nullptr;
			testEqual(vm.getNumLuaValueReferences(), refs_before_script);
		}
	}
	catch(LuaScriptExcepWithLocation& e)
	{
		conPrint("LuaScriptExcepWithLocation: " + e.messageWithLocations());
		failTest(e.what());
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
#endif
}


//========================== Fuzzing ==========================
#if 0
// Fuzzing of the server-side Substrata Lua API (the functions and metamethods registered by SubstrataLuaVM).
// The input is Lua source, which is run as the script of object 123, optionally followed by a zero byte and an HTTP response body.
// After running the script, the event handlers and timers it registered are called, and its HTTP requests are given results: a response
// with the body from the input, or an error if the input has no zero byte.  Then object 124 is deleted and the timers are called again,
// and finally the script is destroyed and its outstanding HTTP requests are given results.
// After each input, checks that all Lua value references the script made have been freed.
//
// Requires a FUZZING build, so that LuaHTTPRequestManager doesn't start worker threads that make real requests.
//
// Command line:
// C:\fuzz_corpus\server_lua_script C:\code\substrata\testfiles\fuzz_seeds\server_lua_script -dict="C:\code\substrata\testfiles\fuzz_seeds\server_lua_script_dictionary.txt" -max_len=20000 -jobs=16

#include <utils/Clock.h>
#include <cstring>


class FuzzLuaScriptOutputHandler : public LuaScriptOutputHandler
{
public:
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override {}
	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override {}
};

static Server* fuzz_server = NULL;
static Reference<SubstrataLuaVM> fuzz_vm;
static FuzzLuaScriptOutputHandler fuzz_output_handler;
static int fuzz_initial_num_lua_value_refs = 0;


extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
	Clock::init();
	LuaVM::staticInit();

	fuzz_server = new Server();
	{
		WorldStateLock lock(fuzz_server->world_state->mutex);
		fuzz_server->world_state->feature_flag_info.feature_flags |= ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG;
	}
	fuzz_vm = new SubstrataLuaVM(SubstrataLuaVM::SubstrataLuaVMArgs(fuzz_server));
	fuzz_initial_num_lua_value_refs = fuzz_vm->getNumLuaValueReferences();
	return 0;
}


// Takes the requests made by scripts off the request queue and gives them results: a response with http_body if has_http_body is true, an error otherwise.
// Then calls think(), which calls the onDone or onError functions.  think() takes the world state lock itself, so it must not be held when calling this.
static void fuzzCompleteHTTPRequests(bool has_http_body, const std::string& http_body)
{
	js::Vector<Reference<LuaHTTPRequest>, 16> requests;
	fuzz_server->lua_http_manager->request_queue.dequeueAnyQueuedItems(requests);

	for(size_t i=0; i<requests.size(); ++i)
	{
		Reference<LuaHTTPRequestResult> result = new LuaHTTPRequestResult();
		result->request = requests[i];
		if(has_http_body)
		{
			result->response.response_code = 200;
			result->response.response_message = "OK";
			result->response.mime_type = "application/json";
			result->data.assign(http_body.begin(), http_body.end());
		}
		else
		{
			result->exception_msg = "Fuzz error";
			result->error_code = LuaHTTPRequestResult::ErrorCode_Other;
		}
		fuzz_server->lua_http_manager->enqueueResult(result);
	}

	fuzz_server->lua_http_manager->think();
}


// Calls the triggered timers like the Server main loop does.
static void fuzzRunTimers(double cur_time, WorldStateLock& lock)
{
	std::vector<ScriptTimerQueueTimer> triggered;
	fuzz_server->timer_queue.update(cur_time, triggered);

	for(size_t i=0; i<triggered.size(); ++i)
	{
		ScriptTimerQueueTimer& timer = triggered[i];
		SubstrataLuaScript* sub_lua_script = timer.sub_lua_script.getPtrIfAlive();
		if(sub_lua_script && (timer.timer_id == sub_lua_script->timers[timer.timer_index].id))
		{
			sub_lua_script->doOnTimerEvent(timer.onTimerEvent_ref, lock);

			if(timer.repeating)
				fuzz_server->timer_queue.reAddRepeatingTimer(cur_time, timer);
			else
				sub_lua_script->destroyTimer(timer.timer_index, timer.timer_id);
		}
	}
}


static void fuzzRunEventHandlers(WorldObject* ob, UID avatar_uid, ParcelID parcel_id, WorldStateLock& lock)
{
	if(!ob->event_handlers)
		return;
	ob->event_handlers->executeOnUserTouchedObjectHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnUserUsedObjectHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnUserMovedNearToObjectHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnUserMovedAwayFromObjectHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnUserEnteredParcelHandlers(avatar_uid, ob->uid, parcel_id, lock);
	ob->event_handlers->executeOnUserExitedParcelHandlers(avatar_uid, ob->uid, parcel_id, lock);
	ob->event_handlers->executeOnUserEnteredVehicleHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnUserExitedVehicleHandlers(avatar_uid, ob->uid, lock);
	ob->event_handlers->executeOnChatMessageHandlers(avatar_uid, "hello", lock);
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	// Split the input into the script source and the HTTP response body, at the first zero byte.
	const uint8_t* zero_byte = (size > 0) ? (const uint8_t*)std::memchr(data, 0, size) : NULL;
	const std::string src((const char*)data, zero_byte ? (size_t)(zero_byte - data) : size);
	const bool has_http_body = zero_byte != NULL;
	const std::string http_body = zero_byte ? std::string((const char*)zero_byte + 1, (size_t)((data + size) - (zero_byte + 1))) : std::string();

	// New manager for each input, so that rate limits and delayed results don't carry over to the next input.
	fuzz_server->lua_http_manager = new LuaHTTPRequestManager(fuzz_server);

	// Make fresh world state and objects for each input, so that changes a script makes don't carry over to the next input.
	Reference<ServerWorldState> world_state = new ServerWorldState();

	WorldObjectRef ob = new WorldObject();
	ob->uid = UID(123);
	ob->materials = { new WorldMaterial(), new WorldMaterial() };

	WorldObjectRef ob2 = new WorldObject();
	ob2->uid = UID(124);

	AvatarRef avatar = new Avatar();
	avatar->uid = UID(456);

	const ParcelID parcel_id(789);

	Reference<SubstrataLuaScript> sub_lua_script;
	double t = 0;
	{
		WorldStateLock lock(fuzz_server->world_state->mutex);

		fuzz_server->world_state->world_states[""] = world_state;
		fuzz_server->world_state->clearObjectStorageItems();
		fuzz_server->timer_queue.clear();

		world_state->getObjects(lock)[ob->uid] = ob;
		world_state->getObjects(lock)[ob2->uid] = ob2;
		world_state->getAvatars(lock)[avatar->uid] = avatar;

		try
		{
			sub_lua_script = new SubstrataLuaScript(fuzz_vm, &fuzz_output_handler, src, ob.ptr(), fuzz_server, world_state.ptr(), lock);
			sub_lua_script->exec(lock);
		}
		catch(glare::Exception&)
		{}

		if(sub_lua_script) // Handlers and timers only exist if the script was created.  The do* functions catch exceptions themselves.
		{
			t = fuzz_server->total_timer.elapsed();

			fuzzRunEventHandlers(ob.ptr(),  avatar->uid, parcel_id, lock);
			fuzzRunEventHandlers(ob2.ptr(), avatar->uid, parcel_id, lock);
			fuzzRunTimers(t + 1000.0, lock);
		}
	}

	if(sub_lua_script)
	{
		for(int i=0; i<3; ++i) // onDone and onError functions can make more requests, so do a few rounds.
			fuzzCompleteHTTPRequests(has_http_body, http_body);

		// Delete object 124, then call the timers again, so that handles to it are used after it has gone.
		WorldStateLock lock(fuzz_server->world_state->mutex);
		world_state->getObjects(lock).erase(ob2->uid);
		ob2->event_handlers = nullptr;
		fuzzRunTimers(t + 2000.0, lock);
	}

	{
		WorldStateLock lock(fuzz_server->world_state->mutex);
		sub_lua_script = nullptr;
	}

	// Give results to requests made by the script that has now been destroyed.
	fuzzCompleteHTTPRequests(has_http_body, http_body);

	// Free everything that holds Lua value references, then check that none were leaked.
	{
		WorldStateLock lock(fuzz_server->world_state->mutex);
		ob->event_handlers = nullptr;
		ob2->event_handlers = nullptr;
		fuzz_server->timer_queue.clear();
		testEqual(fuzz_vm->getNumLuaValueReferences(), fuzz_initial_num_lua_value_refs);
	}

	return 0; // Non-zero return values are reserved for future use.
}
#endif
//========================== End fuzzing ==========================


#endif // BUILD_TESTS
