/*=====================================================================
ObjectEventHandlers.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ObjectEventHandlers.h"


void HandlerList::addHandler(const HandlerFunc& handler)
{
	// See if it exists already
	for(size_t i=0; i<handler_funcs.size(); ++i)
		if(handler_funcs[i] == handler)
			return;

	// Add
	handler_funcs.push_back(handler);
}


void HandlerList::removeHandler(const HandlerFunc& handler)
{
	for(size_t i=0; i<handler_funcs.size(); ++i)
		if(handler_funcs[i] == handler)
		{
			removeHandlerAtIndex(i);
			return;
		}
}


void HandlerList::removeHandlerAtIndex(size_t i)
{
	// Swap with last element, if this is not the last element.
	if(i + 1 < handler_funcs.size())
		mySwap(handler_funcs[i], handler_funcs[handler_funcs.size() - 1]);

	handler_funcs.pop_back(); // Now remove it
}


void ObjectEventHandlers::executeOnUserUsedObjectHandlers(UID avatar_uid, UID ob_uid)
{
	// Execute doOnUserUsedObject event handler in any other scripts that are listening for doOnUserUsedObject for this object
	for(size_t z=0; z<onUserUsedObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserUsedObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserUsedObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid);
			z++;
		}
		else
			onUserUsedObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	} // The loop is guaranteed to terminate because we either increment z or decrease the size of handler_funcs (in removeHandlerAtIndex) at each iteration.
}


void ObjectEventHandlers::executeOnUserTouchedObjectHandlers(UID avatar_uid, UID ob_uid, double cur_time)
{
	// Execute doOnUserTouchedObject event handler in any other scripts that are listening for onUserTouchedObject for this object
	for(size_t z=0; z<onUserTouchedObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserTouchedObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserTouchedObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, cur_time);
			z++;
		}
		else
			onUserUsedObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserMovedNearToObjectHandlers(UID avatar_uid, UID ob_uid)
{
	// Execute doOnUserMovedNearToObject event handler in any other scripts that are listening for onUserMovedNearToObject for this object
	for(size_t z=0; z<onUserMovedNearToObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserMovedNearToObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserMovedNearToObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid);
			z++;
		}
		else
			onUserMovedNearToObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserMovedAwayFromObjectHandlers(UID avatar_uid, UID ob_uid)
{
	// Execute doOnUserMovedAwayFromObject event handler in any other scripts that are listening for onUserMovedAwayFromObject for this object
	for(size_t z=0; z<onUserMovedAwayFromObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserMovedAwayFromObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserMovedAwayFromObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid);
			z++;
		}
		else
			onUserMovedAwayFromObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserEnteredParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id)
{
	for(size_t z=0; z<onUserEnteredParcel_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserEnteredParcel_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserEnteredParcel(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, parcel_id);
			z++;
		}
		else
			onUserEnteredParcel_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserExitedParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id)
{
	for(size_t z=0; z<onUserExitedParcel_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserExitedParcel_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserExitedParcel(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, parcel_id);
			z++;
		}
		else
			onUserExitedParcel_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}
