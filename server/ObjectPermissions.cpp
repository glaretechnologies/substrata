/*=====================================================================
ObjectPermissions.cpp
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ObjectPermissions.h"


#include "ServerWorldState.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include <BitUtils.h>


bool posIsInParcelForWhichLoggedInUserHasWritePerms(const Vec3d& pos, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	assert(user_id.valid());

	const Vec4f pos_vec4f = pos.toVec4fPoint();

	ServerWorldState::ParcelMapType& parcels = world_state.getParcels(lock);
	for(ServerWorldState::ParcelMapType::iterator it = parcels.begin(); it != parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		if(parcel->pointInParcel(pos_vec4f) && parcel->userHasWritePerms(user_id))
			return true;
	}

	return false;
}


bool objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	return posIsInParcelForWhichLoggedInUserHasWritePerms(ob.pos, user_id, world_state, lock);
}


// Is the client connected to a world that the user is the owner of?
bool connectedToUsersWorld(const UserID& user_id, ServerWorldState& connected_world)
{
	assert(user_id.valid());

	return connected_world.details.owner_id == user_id;
}


// NOTE: world state mutex should be locked before calling this method.
bool userHasObjectWritePermissions(const WorldObject& ob, const UserID& user_id, const std::string& user_name, ServerWorldState& world_state, bool allow_light_mapper_bot_full_perms,
	WorldStateLock& lock)
{
	if(user_id.valid())
	{
		return (user_id == ob.creator_id) || // If the user created/owns the object
			isGodUser(user_id) || // or if the user is the god user (id 0)
			(allow_light_mapper_bot_full_perms && (user_name == "lightmapperbot")) || // lightmapper bot has full write permissions for now.
			connectedToUsersWorld(user_id, world_state) || // or if the user owns this world
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob, user_id, world_state, lock); // Can modify objects owned by other people if they are in parcels you have write permissions for.
	}
	else
		return false;
}


// Does the user have permission to create a summoned object (e.g. summoned vehicle)?
// For now just check the SUMMONED_FLAG and check the model URL is one of the vehicle model URLs from GUIClient.cpp.
bool userCanCreateSummonedObject(const WorldObject& ob, const UserID& user_id)
{
	if(BitUtils::isBitSet(ob.flags, WorldObject::SUMMONED_FLAG))
	{
		if(ob.model_url == "deLorean2_0_glb_5923323464955550713.bmesh" || // car
			ob.model_url == "optimized_dressed_fix7_offset4_glb_4474648345850208925.bmesh" || // bike
			ob.model_url == "peugot_closed_glb_2887717763908023194.bmesh" || // hovercar
			ob.model_url == "poweryacht3_2_glb_17116251394697619807.bmesh" || // boat
			ob.model_url == "Jet_Ski_obj_3200017390617214853.bmesh") // jetski
			return true;
		else
			return false;
	}
	else
		return false;
}


// Does the user have permission to create the given object with its current transformation?
// NOTE: world state mutex should be locked before calling this method.
bool userHasObjectCreationPermissions(const WorldObject& ob, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	if(user_id.valid())
	{
		return isGodUser(user_id) || // if the user is the god user
			connectedToUsersWorld(user_id, world_state) || // or if this is the user's world
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob, user_id, world_state, lock) || // Or this object is in a parcel we have write permissions for.
			userCanCreateSummonedObject(ob, user_id);
	}
	else
		return false;
}


// Does the user have permission to create an object at, or move an object to, the given position?
// NOTE: world state mutex should be locked before calling this method.
bool userHasObjectCreationPermissionsAtPos(const Vec3d& pos, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	if(user_id.valid())
	{
		return isGodUser(user_id) || // if the user is the god user
			connectedToUsersWorld(user_id, world_state) || // or if this is the user's world
			posIsInParcelForWhichLoggedInUserHasWritePerms(pos, user_id, world_state, lock); // Or the position is in a parcel we have write permissions for.
	}
	else
		return false;
}
