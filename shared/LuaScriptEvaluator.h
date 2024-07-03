/*=====================================================================
LuaScriptEvaluator.h
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include "UID.h"
#include "ParcelID.h"
#include <lua/LuaScript.h>
#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/WeakRefCounted.h>
#include <utils/UniqueRef.h>
#include <memory>
#include <lua.h> // For LUA_NOREF
class SubstrataLuaVM;
class WorldObject;
class ServerWorldState;
class WorldStateLock;


/*=====================================================================
LuaScriptEvaluator
------------------
Per-WorldObject
=====================================================================*/
class LuaScriptEvaluator : public WeakRefCounted
{
public:
	LuaScriptEvaluator(SubstrataLuaVM* substrata_lua_vm, LuaScriptOutputHandler* script_output_handler, 
		const std::string& script_src, WorldObject* world_object,
#if SERVER
		ServerWorldState* world_state, // The world that the object belongs to.
#endif
		WorldStateLock& world_state_lock // Since this constructor executes Lua code, we need to hold the world state lock
	);
	~LuaScriptEvaluator();


	void doOnUserTouchedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept;
	bool isOnUserTouchedObjectDefined() { return onUserTouchedObject_ref != LUA_NOREF; }
	
	void doOnUserUsedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	bool isOnUserUsedObjectDefined() { return onUserUsedObject_ref != LUA_NOREF; }

	void doOnUserMovedNearToObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	bool isOnUserMovedNearToObjectDefined() { return onUserMovedNearToObject_ref != LUA_NOREF; }
	
	void doOnUserMovedAwayFromObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	bool isOnUserMovedAwayFromObjectDefined() { return onUserMovedAwayFromObject_ref != LUA_NOREF; }

	void doOnUserEnteredParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	bool isOnUserEnteredParcelDefined() { return onUserEnteredParcel_ref != LUA_NOREF; }

	void doOnUserExitedParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	bool isOnUserExitedParcelDefined() { return onUserExitedParcel_ref != LUA_NOREF; }

	void doOnTimerEvent(int onTimerEvent_ref, WorldStateLock& world_state_lock) noexcept;

	void destroyTimer(int timer_index);

//private:
	void pushUserTableOntoStack(UserID client_user_id);
	void pushAvatarTableOntoStack(UID avatar_uid);
	void pushWorldObjectTableOntoStack(UID ob_uid); // OLD: Push a table for this->world_object onto Lua stack.
	void pushParcelTableOntoStack(ParcelID parcel_id);
public:

	SubstrataLuaVM* substrata_lua_vm;
	UniqueRef<LuaScript> lua_script;
	LuaScriptOutputHandler* script_output_handler;
	bool hit_error;

	WorldObject* world_object;
#if SERVER
	ServerWorldState* world_state; // The world that the object belongs to.
#endif

	WorldStateLock* cur_world_state_lock; // Non-null if the world state lock is currently held by this thread, null otherwise.

	static const int MAX_NUM_TIMERS = 4;

	struct LuaTimerInfo
	{
		int id; // -1 means no timer.
		int onTimerEvent_ref; // Reference to Lua callback function
	};
	LuaTimerInfo timers[MAX_NUM_TIMERS];

	int next_timer_id;

	int num_obs_event_listening; // Number of objects that this script has added an event listener to.

	int onUserTouchedObject_ref;
	int onUserUsedObject_ref;
	int onUserMovedNearToObject_ref;
	int onUserMovedAwayFromObject_ref;
	int onUserEnteredParcel_ref;
	int onUserExitedParcel_ref;
};
