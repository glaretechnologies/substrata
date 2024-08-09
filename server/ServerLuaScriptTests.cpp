/*=====================================================================
ServerLuaScriptTests.cpp
------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ServerLuaScriptTests.h"


#if BUILD_TESTS


#include "Server.h"
#include "ServerWorldState.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/WorldObject.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/WorldStateLock.h"
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/TestUtils.h>
#include <utils/TestExceptionUtils.h>
#include <lualib.h>


class ServerTestLuaScriptOutputHandler : public LuaScriptOutputHandler
{
public:
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len)
	{
		conPrint("Test Lua: " + std::string(s, len));
		buf += std::string(s, len);
	}

	virtual void errorOccurred(LuaScript* script, const std::string& msg)
	{
		conPrint("Test Lua error: " + msg);
		buf += msg;
	}

	std::string buf;
};


void ServerLuaScriptTests::test()
{
	try
	{
		Server server;

		WorldStateLock lock(server.world_state->mutex);

		Reference<ServerWorldState> main_world_state = new ServerWorldState();
		server.world_state->world_states[""] = main_world_state;


		SubstrataLuaVM vm;
		vm.setAsNotIndependentlyHeapAllocated();
		vm.server = &server;

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
		

		{
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, "print('hello')", world_ob.ptr(), main_world_state.ptr(), lock);
		}

		//-------------------------------- Test this_object --------------------------------
		{
			const std::string script_src = "print('this_object.uid: ' .. tostring(this_object.uid))  assert(this_object.uid == 123.0)\n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "this_object.uid: 123");
		}

		//-------------------------------- Test IS_SERVER --------------------------------
		{
			const std::string script_src = "print('IS_SERVER: ' .. tostring(IS_SERVER))  assert(IS_SERVER == true)\n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "IS_SERVER: true");
		}

		//-------------------------------- Test IS_CLIENT --------------------------------
		{
			const std::string script_src = "print('IS_CLIENT: ' .. tostring(IS_CLIENT))  assert(IS_CLIENT == false)\n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "IS_CLIENT: false");
		}

		//-------------------------------- Test doOnUserTouchedObject --------------------------------
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserTouchedObject(world_ob->getOrCreateEventHandlers()->onUserTouchedObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 touched object 123");
		}

		//-------------------------------- Test doOnUserUsedObject --------------------------------
		{
			const std::string script_src = 
				"function onUserUsedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' used object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserUsedObject_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserUsedObject(world_ob->getOrCreateEventHandlers()->onUserUsedObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 used object 123");
		}

		//-------------------------------- Test doOnUserMovedNearToObject --------------------------------
		{
			const std::string script_src = 
				"function onUserMovedNearToObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' moved near to object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserMovedNearToObject(world_ob->getOrCreateEventHandlers()->onUserMovedNearToObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 moved near to object 123");
		}

		//-------------------------------- Test doOnUserMovedAwayFromObject --------------------------------
		{
			const std::string script_src = 
				"function onUserMovedAwayFromObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' moved away from object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserMovedAwayFromObject(world_ob->getOrCreateEventHandlers()->onUserMovedAwayFromObject_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 moved away from object 123");
		}

		//-------------------------------- Test doOnUserEnteredParcel --------------------------------
		{
			const std::string script_src = 
				"function onUserEnteredParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' entered parcel ' .. tostring(parcel.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserEnteredParcel(world_ob->getOrCreateEventHandlers()->onUserEnteredParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 entered parcel 789");
		}

		//-------------------------------- Test doOnUserExitedParcel --------------------------------
		{
			const std::string script_src = 
				"function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' exited parcel ' .. tostring(parcel.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 exited parcel 789");
		}

		//-------------------------------- Test onUserEnteredVehicle --------------------------------
		{
			const std::string script_src = 
				"function onUserEnteredVehicle(av : Avatar, vehicle_ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' entered vehicle ' .. tostring(vehicle_ob.uid))			\n"
				"end \n"
				"addEventListener('onUserEnteredVehicle', 123, onUserEnteredVehicle)   \n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserEnteredVehicle_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserEnteredVehicle(world_ob->getOrCreateEventHandlers()->onUserEnteredVehicle_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 entered vehicle 123");
		}

		//-------------------------------- Test onUserExitedVehicle --------------------------------
		{
			const std::string script_src = 
				"function onUserExitedVehicle(av : Avatar, vehicle_ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' exited vehicle ' .. tostring(vehicle_ob.uid))			\n"
				"end \n"
				"addEventListener('onUserExitedVehicle', 123, onUserExitedVehicle)   \n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedVehicle_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserExitedVehicle(world_ob->getOrCreateEventHandlers()->onUserExitedVehicle_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 exited vehicle 123");
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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(lua_script_evaluator->timers[0].id == lua_script_evaluator->next_timer_id - 1);
			
			lua_script_evaluator->doOnTimerEvent(lua_script_evaluator->timers[0].onTimerEvent_ref, lock);

			testAssert(!lua_script_evaluator->hit_error);
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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(lua_script_evaluator->timers[0].id == -1);

			testAssert(!lua_script_evaluator->hit_error);
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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			for(int i=0; i<4; ++i)
				testAssert(lua_script_evaluator->timers[i].id == -1);

			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "");
		}

		// The script creates a timer with a nil function, then we try and call it.  Check doesn't crash etc.
		{
			const std::string script_src =  
				"local timer_handle = createTimer(nil, 0.1, false)	";
			// Should get: createTimer(): arg 1 must be a function
			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); },
				"arg 1 must be a function");
		}

		// Test creating too many timers
		{
			const std::string script_src =  
				"function onTimerEvent(ob : Object)		\n"
				"		print('onTimerEvent')			\n"
				"end									\n"
				"for i=1, 10 do  createTimer(onTimerEvent, 0.1, false)	end";
			// Should get: createTimer(): arg 1 must be a function
			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); },
				"Could not create timer");
		}

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

			temp_world_ob->lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, temp_world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(temp_world_ob->lua_script_evaluator->timers[0].id == temp_world_ob->lua_script_evaluator->next_timer_id - 1);
			
			// The timer event should have got added to the server timer queue.  Dequeue it.
			std::vector<TimerQueueTimer> triggered_timers;
			server.timer_queue.update(/*cur time=*/server.total_timer.elapsed() + 1.0, triggered_timers);
			testAssert(triggered_timers.size() == 1);

			testAssert(triggered_timers[0].lua_script_evaluator.getPtrIfAlive() == temp_world_ob->lua_script_evaluator.ptr());

			// Delete the ob
			main_world_state->getObjects(lock).erase(temp_world_ob->uid);
			temp_world_ob = nullptr;

			// Test the weak reference notices that the object and its lua_script_evaluator has been destroyed
			testAssert(triggered_timers[0].lua_script_evaluator.getPtrIfAlive() == nullptr);
		}

		// Test adding a timer inside a timer event handler 
		{
			const std::string script_src = 
				"function onTimerEvent(ob : Object)								\n"
				"		print('onTimerEvent')									\n"
				"		createTimer(onTimerEvent, 0.1, false)					\n" // Create another timer!
				"end															\n"
				"createTimer(onTimerEvent, 0.1, false)							\n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(lua_script_evaluator->timers[0].id == lua_script_evaluator->next_timer_id - 1);

			lua_script_evaluator->doOnTimerEvent(lua_script_evaluator->timers[0].onTimerEvent_ref, lock);

			testAssert(!lua_script_evaluator->hit_error);
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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(lua_script_evaluator->timers[0].id == lua_script_evaluator->next_timer_id - 1);

			lua_script_evaluator->doOnTimerEvent(lua_script_evaluator->timers[0].onTimerEvent_ref, lock);

			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "onTimerEvent");
		}


		//-------------------------------- Test getObjectForUID --------------------------------
		{ // Test successful getObjectForUID call
			const std::string script_src = "local ob = getObjectForUID(123)";
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
		}
		{ // Test unsuccessful getObjectForUID call
			const std::string script_src = "local ob = getObjectForUID(6544234)";
			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "No object with UID 6544234");
		}

		//-------------------------------- Test getCurrentTime --------------------------------
		{
			const std::string script_src = "getCurrentTime()";
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
		}

		//-------------------------------- Test showMessageToUser --------------------------------
		{
			const std::string script_src = "local avatar = {uid=456}   showMessageToUser('hello thar', avatar)";
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
		}
		{ // Test invalid showMessageToUser call (avatar has no uid field)
			const std::string script_src = "local avatar = {}   showMessageToUser('hello thar', avatar)";
			testExceptionExpected([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); });
		}
		{ // Test invalid showMessageToUser call (avatar not a table)
			const std::string script_src = "local avatar = 123.0   showMessageToUser('hello thar', avatar)";
			testExceptionExpected([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); });
		}

		//-------------------------------- Test addEventListener --------------------------------
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', 124, onUserTouchedObject)			";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.nonEmpty());
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob->event_handlers && world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());

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

			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "Unknown event");
		}

		// Test invalid addEventListener call - no such object with UID
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"addEventListener('notAnEvent', 124111111, onUserTouchedObject)			\n";

			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "No object");
		}

		// Test invalid addEventListener calls - trying to add too many event listeners
		// NOTE: test disabled since adding multiple of the same listener doesn't increment listener count now.
		/*{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"for i=1, 10000 do addEventListener('onUserTouchedObject', 124, onUserTouchedObject)		end	\n";

			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "too many event listeners");
		}*/

		// Test addEventListener called from a script that is then destroyed.
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

			temp_world_ob->lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, temp_world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 1);
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == temp_world_ob->lua_script_evaluator.ptr());

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

		//-------------------------------- Test objectstorage.setItem  --------------------------------
		{
			const std::string script_src = "objectstorage.setItem(\"a\", 1230.0)";

			server.world_state->object_storage_items.clear();

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

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
				Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);

			for(int i=0; i<2000; ++i)
				lua_script_evaluator->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(lua_script_evaluator->hit_error);
		}

		// Test calling objectstorage.setItem with a non-string key
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.setItem({a=1.0}, 1000.0)";
				Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"Argument 1 was not a string");
		}

		// Test with a key that is too long
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.setItem('" + std::string(1000, 'a') + "', 1000.0)";
				Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
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
				Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
		}

		// Test serialisaing and deserialising a Vec3d
		{
			const std::string script_src = 
				"objectstorage.setItem(\"a\", Vec3d(1.0, 2.0, 3.0))				\n"
				"local v = objectstorage.getItem(\"a\")							\n"
				"assert(v == Vec3d(1.0, 2.0, 3.0))								\n"
				"v2 = v + v														\n" // Test metatable is correct and that __add is called.
				"assert(v2 == Vec3d(2.0, 4.0, 6.0))								\n";

			server.world_state->clearObjectStorageItems();

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!lua_script_evaluator->hit_error);
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
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs.size() == 1);
			lua_script_evaluator->doOnUserExitedParcel(world_ob->getOrCreateEventHandlers()->onUserExitedParcel_handlers.handler_funcs[0].handler_func_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test calling objectstorage.getItem with a key that is not present returns nil
		{
			const std::string script_src = 
				"local x = objectstorage.getItem(\"a\")			\n"
				"assert(x == nil)								\n";

			server.world_state->clearObjectStorageItems();

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			testAssert(server.world_state->object_storage_items.size() == 0); // Check didn't add item
		}

		// Test calling objectstorage.getItem with a non-string key
		{
			testThrowsExcepContainingString([&]() { 
				const std::string script_src = "objectstorage.getItem({a=1.0})";
				Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"Argument 1 was not a string");
		}

		//-------------------------------- Test parseJSON  --------------------------------
		{
			const std::string script_src = 
				"local x = parseJSON('123.456')						\n"
				"assert(x == 123.456)								\n";

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
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

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test converting an empty object
		{
			const std::string script_src = 
				"local x = parseJSON('{}')										\n"
				"print(x)														\n"
				"assert(#x == 0)												\n";

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test converting an empty array
		{
			const std::string script_src = 
				"local x = parseJSON('[]')										\n"
				"print(x)														\n"
				"assert(#x == 0)												\n";

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test converting just null
		{
			const std::string script_src = 
				"local x = parseJSON('null')									\n"
				"assert(x == nil)												\n";

			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test converting a deeply nested object
		{
			std::string script_src = "local x = parseJSON('";
			for(int i=0; i<100; ++i)
				script_src += std::string("{ \"a\" : ");
			script_src += "{}" + std::string(100, '}') + "')						\n"
				"												\n";
			//conPrint(script_src);
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}
		// Test converting a deeply nested array
		{
			std::string script_src = "local x = parseJSON('" + 
				std::string(100, '[') + "[]" + std::string(100, ']') + "')						\n";
			//conPrint(script_src);
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(!lua_script_evaluator->hit_error);
		}

		// Test some invalid json
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON(' ')    ";
				new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON('[[[')    ";
				new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON('truAAA')    ";
				new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"Error while parsing JSON:"
		);
		testThrowsExcepContainingString([&]()
			{
				std::string script_src = "local x = parseJSON(true)    ";
				new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			},
			"was not a string"
		);
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
}


#endif // BUILD_TESTS
