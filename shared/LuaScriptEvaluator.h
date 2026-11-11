/*=====================================================================
LuaScriptEvaluator.h
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include "UID.h"
#include "ParcelID.h"
#include "DependencyURL.h"
#include <lua/LuaScript.h>
#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/WeakRefCounted.h>
#include <utils/UniqueRef.h>
#include <utils/string_view.h>
#include <memory>
#include <set>
class SubstrataLuaVM;
class RateLimiter;
class WorldObject;
class ServerWorldState;
class WorldStateLock;
class LuaHTTPRequestResult;


/*=====================================================================
LuaScriptEvaluator
------------------
Per-WorldObject
=====================================================================*/
class LuaScriptEvaluator : public WeakRefCounted
{
public:
	LuaScriptEvaluator(const Reference<SubstrataLuaVM>& substrata_lua_vm, LuaScriptOutputHandler* script_output_handler, 
		const std::string& script_src, WorldObject* world_object,
#if SERVER
		ServerWorldState* world_state, // The world that the object belongs to.
#endif
		WorldStateLock& world_state_lock // Since this constructor executes Lua code, we need to hold the world state lock
	);
	~LuaScriptEvaluator();

	void exec(WorldStateLock& world_state_lock); // Execute top-level Lua code.

	void doOnUserTouchedObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept;
	
	void doOnUserUsedObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserMovedNearToObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	
	void doOnUserMovedAwayFromObject(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserEnteredParcel(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserExitedParcel(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserEnteredVehicle(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	
	void doOnUserExitedVehicle(LuaVM::LuaValueRef func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnChatMessage(LuaVM::LuaValueRef func_ref, UID avatar_uid, const std::string& message, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnTimerEvent(LuaVM::LuaValueRef onTimerEvent_ref, WorldStateLock& world_state_lock) noexcept;

	void doOnUIEvent(LuaVM::LuaValueRef func_ref, UID avatar_uid, WorldStateLock& world_state_lock) noexcept; // For callbacks from UI widgets created by the script, such as button onClick functions.

	void destroyTimer(int timer_index, int timer_id);

	void doOnError(LuaVM::LuaValueRef onError_ref, int error_code, const std::string& error_description, WorldStateLock& world_state_lock) noexcept;
	void doOnDone(LuaVM::LuaValueRef onDone_ref, Reference<LuaHTTPRequestResult> result, WorldStateLock& world_state_lock) noexcept;

	// A Lua script declares the resources it uses (such as sounds for playSound()) in comment lines straight after its first '--lua' line:
	//   --lua
	//   -- @resource door_open_1938475.mp3
	// The declarations end at the first line that is neither blank nor a comment.
	// At most MAX_NUM_RESOURCE_URLS declarations are read, any after that are ignored.
	static const size_t MAX_NUM_RESOURCE_URLS = 20;
	static void appendScriptResourceURLs(const string_view script, glare::ArenaAllocator* allocator, DependencyURLVector& urls_out);

	// Adds a "-- @resource URL" line to the script header, after any existing @resource lines, or straight after the '--lua' line if there are none.
	// An empty script becomes a Lua script with just the header.
	// Returns false, leaving script unchanged, if the script is not a Lua script.
	static bool addScriptResourceDeclaration(std::string& script, const string_view URL);

	// Is URL declared in this script's header with @resource?
	bool isResourceURLDeclared(const string_view URL) const;

	// Prints a warning to the script output that URL is not declared with @resource.  Only warns once for each URL, so a timer calling playSound() with a wrong URL doesn't flood the output.
	// URLs longer than WorldObject::MAX_URL_SIZE are ignored, without a warning.
	void warnAboutUndeclaredResourceURL(const string_view func_name, const string_view URL);

	static void test();

//private:
	void removeScriptUIWidgets(); // Removes any UI widgets the script created (client only).

	void pushUserTableOntoStack(lua_State* state, UserID client_user_id);
	void pushAvatarTableOntoStack(lua_State* state, UID avatar_uid);
	void pushWorldObjectTableOntoStack(lua_State* state, UID ob_uid); // OLD: Push a table for this->world_object onto Lua stack.
	void pushParcelTableOntoStack(lua_State* state, ParcelID parcel_id);
public:
	Reference<SubstrataLuaVM> substrata_lua_vm;
	UniqueRef<LuaScript> lua_script;
	LuaScriptOutputHandler* script_output_handler;
	bool hit_error; // have we caught an exception while executing Lua code for this script?  If so, don't execute any more handlers.

	WorldObject* world_object;
#if SERVER
	ServerWorldState* world_state; // The world that the object belongs to.
#endif

	WorldStateLock* cur_world_state_lock; // Non-null if the world state lock is currently held by this thread, null otherwise.

	UID calling_avatar_uid; // The avatar that triggered the event currently being handled (e.g. the avatar that touched the object).  Invalid for events not triggered by an avatar, such as timers.

	static const int MAX_NUM_TIMERS = 4;

	struct LuaTimerInfo
	{
		int id; // -1 means no timer.
		LuaVM::LuaValueRef onTimerEvent_ref; // Reference to Lua callback function
	};
	LuaTimerInfo timers[MAX_NUM_TIMERS];

	int next_timer_id;

	int num_obs_event_listening; // Number of objects that this script has added an event listener to.


	DependencyURLVector resource_URLs; // Resources declared in the script header.  See appendScriptResourceURLs().

	std::set<std::string> warned_undeclared_resource_URLs; // URLs that warnAboutUndeclaredResourceURL() has already warned about.

#if SERVER
	Reference<RateLimiter> play_sound_rate_limiter; // Limits how often this script can broadcast ScriptPlaySound messages.
#endif
};
