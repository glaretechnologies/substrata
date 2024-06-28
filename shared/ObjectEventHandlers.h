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


struct HandlerFunc
{
	WeakReference<LuaScriptEvaluator> script;
	int handler_func_ref;

	bool operator == (const HandlerFunc& other) const { return script.ob == other.script.ob && handler_func_ref == other.handler_func_ref; }
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
	void executeOnUserUsedObjectHandlers(UID avatar_uid, UID ob_uid);
	void executeOnUserTouchedObjectHandlers(UID avatar_uid, UID ob_uid, double cur_time);
	void executeOnUserMovedNearToObjectHandlers(UID avatar_uid, UID ob_uid);
	void executeOnUserMovedAwayFromObjectHandlers(UID avatar_uid, UID ob_uid);
	void executeOnUserEnteredParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id);
	void executeOnUserExitedParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id);

	HandlerList onUserUsedObject_handlers;
	HandlerList onUserTouchedObject_handlers;
	HandlerList onUserMovedNearToObject_handlers;
	HandlerList onUserMovedAwayFromObject_handlers;
	HandlerList onUserEnteredParcel_handlers;
	HandlerList onUserExitedParcel_handlers;
};
