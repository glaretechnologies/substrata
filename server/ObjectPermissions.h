/*=====================================================================
ObjectPermissions.h
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/WorldStateLock.h"
#include <string>
class WorldObject;
class ServerWorldState;


/*=====================================================================
ObjectPermissions
-----------------
Permission-check helpers for creating and modifying world objects.

Shared between the game WorkerThread (handling connected clients) and the MCP
server (handling requests from AI agents), so that both enforce the same rules.

NOTE: the world state mutex must be held when calling these.
=====================================================================*/

// Is the given user the owner of the world 'connected_world'?
bool connectedToUsersWorld(const UserID& user_id, ServerWorldState& connected_world);

// Is the object located in a parcel that the user has write permissions for?
bool objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock);

// Does the user have permission to create a summoned object (e.g. summoned vehicle)?
bool userCanCreateSummonedObject(const WorldObject& ob, const UserID& user_id);

// Does the user have permission to modify the given (existing) object?
bool userHasObjectWritePermissions(const WorldObject& ob, const UserID& user_id, const std::string& user_name, ServerWorldState& world_state, bool allow_light_mapper_bot_full_perms,
	WorldStateLock& lock);

// Does the user have permission to create the given object with its current transformation?
bool userHasObjectCreationPermissions(const WorldObject& ob, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock);
