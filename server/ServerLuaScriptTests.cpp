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

		WorldObjectRef world_ob2 = new WorldObject();
		world_ob2->uid = UID(124);

		AvatarRef avatar = new Avatar();
		avatar->uid = UID(456);

		ParcelRef parcel = new Parcel();
		parcel->id = ParcelID(789);

		main_world_state->getObjects(lock)[world_ob->uid] = world_ob;
		main_world_state->getObjects(lock)[world_ob2->uid] = world_ob2;
		main_world_state->getAvatars(lock)[avatar->uid] = avatar;
		

		{
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, "print('hello')", world_ob.ptr(), main_world_state.ptr(), lock);
		}

		//-------------------------------- Test doOnUserTouchedObject --------------------------------
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);
			testAssert(lua_script_evaluator->isOnUserTouchedObjectDefined());
			lua_script_evaluator->doOnUserTouchedObject(lua_script_evaluator->onUserTouchedObject_ref, avatar->uid, world_ob->uid, lock);
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
			testAssert(lua_script_evaluator->isOnUserUsedObjectDefined());
			lua_script_evaluator->doOnUserUsedObject(lua_script_evaluator->onUserUsedObject_ref, avatar->uid, world_ob->uid, lock);
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
			testAssert(lua_script_evaluator->isOnUserMovedNearToObjectDefined());
			lua_script_evaluator->doOnUserMovedNearToObject(lua_script_evaluator->onUserMovedNearToObject_ref, avatar->uid, world_ob->uid, lock);
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
			testAssert(lua_script_evaluator->isOnUserMovedAwayFromObjectDefined());
			lua_script_evaluator->doOnUserMovedAwayFromObject(lua_script_evaluator->onUserMovedAwayFromObject_ref, avatar->uid, world_ob->uid, lock);
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
			testAssert(lua_script_evaluator->isOnUserEnteredParcelDefined());
			lua_script_evaluator->doOnUserEnteredParcel(lua_script_evaluator->onUserEnteredParcel_ref, avatar->uid, world_ob->uid, parcel->id, lock);
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
			testAssert(lua_script_evaluator->isOnUserExitedParcelDefined());
			lua_script_evaluator->doOnUserExitedParcel(lua_script_evaluator->onUserExitedParcel_ref, avatar->uid, world_ob->uid, parcel->id, lock);
			testAssert(!lua_script_evaluator->hit_error);
			testAssert(output_handler.buf == "Avatar 456 exited parcel 789");
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
				"addEventListener('onUserTouchedObject', getObjectForUID(124), onUserTouchedObject)			";

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
				"addEventListener('onUserTouchedObject', getObjectForUID(124), onUserTouchedObject)			\n"
				"addEventListener('onUserTouchedObject', getObjectForUID(124), onUserTouchedObject)			\n";

			output_handler.buf.clear();
			Reference<LuaScriptEvaluator> lua_script_evaluator = new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock);

			// NOTE: Adds two different handlers since the lua reference to the function differs each time we call lua_ref.
			testAssert(world_ob2->event_handlers && world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs.size() == 2);
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[0].script.getPtrIfAlive() == lua_script_evaluator.ptr());
			testAssert(world_ob2->event_handlers->onUserTouchedObject_handlers.handler_funcs[1].script.getPtrIfAlive() == lua_script_evaluator.ptr());

			// Execute the event handler
			world_ob2->event_handlers->executeOnUserTouchedObjectHandlers(avatar->uid, world_ob2->uid, lock);

			testEqual(output_handler.buf, std::string("Avatar 456 touched object 124Avatar 456 touched object 124")); // NOTE: saying touched 124 here (world_ob2)

			world_ob2->event_handlers = NULL; // Clean up from test
		}

		// Test invalid addEventListener call - invalid event
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"addEventListener('notAnEvent', getObjectForUID(124), onUserTouchedObject)			\n";

			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "Unknown event");
		}

		// Test invalid addEventListener calls - trying to add too many event listeners
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"end				\n"
				"for i=1, 10000 do addEventListener('onUserTouchedObject', getObjectForUID(124), onUserTouchedObject)		end	\n";

			testThrowsExcepContainingString([&]() { new LuaScriptEvaluator(&vm, &output_handler, script_src, world_ob.ptr(), main_world_state.ptr(), lock); }, "too many event listeners");
		}

		// Test addEventListener called from a script that is then destroyed.
		{
			const std::string script_src = 
				"function onUserTouchedObject(av : Avatar, ob : Object)			\n"
				"		print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid))			\n"
				"end				\n"
				"addEventListener('onUserTouchedObject', getObjectForUID(124), onUserTouchedObject)			";

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
