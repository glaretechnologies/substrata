/*=====================================================================
ObjectEventHandlers.h
---------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "LuaScriptEvaluator.h"
#include <utils/ThreadSafeRefCounted.h>
#include <utils/WeakReference.h>
#include <vector>
class WorldStateLock;


struct HandlerFunc
{
	WeakReference<LuaScriptEvaluator> script;
	int handler_func_ref;
	const void* function_ptr; // Just used for uniquely identifying functions, not dereferenced.

	bool operator == (const HandlerFunc& other) const { return script.ob == other.script.ob && function_ptr == other.function_ptr; }
};

struct HandlerList
{
	void addHandler(const HandlerFunc& handler);
	void removeHandler(const HandlerFunc& handler);
	void removeHandlerAtIndex(size_t i);

	bool nonEmpty() { return !handler_funcs.empty(); }

	std::vector<HandlerFunc> handler_funcs;
};


/*=====================================================================
ObjectEventHandlers
-------------------
An object script can listen for events that are triggered on other objects, for example onUserUsedObject.

onUserUsedObject_handlers is a list of all scripts handler functions that are listening for the user-used-object event
on the object that owns the ObjectEventHandlers instance.
=====================================================================*/
class ObjectEventHandlers : public ThreadSafeRefCounted
{
public:
	void executeOnUserUsedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserTouchedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserMovedNearToObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserMovedAwayFromObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserEnteredParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock);
	void executeOnUserExitedParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock);

	HandlerList onUserUsedObject_handlers;
	HandlerList onUserTouchedObject_handlers;
	HandlerList onUserMovedNearToObject_handlers;
	HandlerList onUserMovedAwayFromObject_handlers;
	HandlerList onUserEnteredParcel_handlers;
	HandlerList onUserExitedParcel_handlers;
};
