/*=====================================================================
WorldMaintenance.cpp
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "WorldMaintenance.h"


#include <graphics/Map2D.h>
#include <graphics/PNGDecoder.h>
#include <PCG32.h>
#include <Rect2.h>
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <FileChecksum.h>
#include <Lock.h>


// From GUIClient::summonBike()
static const Colour3f bike_default_cols[] = {
	Colour3f(0.20655301,0.20655301,0.20655301),
	Colour3f(0.04653936,0.04653936,0.04653936),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.3999135,0.3999135,0.3999135),
	Colour3f(0.52657396,0.52657396,0.52657396),
	Colour3f(0,0,0),
	Colour3f(0.039884407,0.039884407,0.039884407),
	Colour3f(0.039884407,0.039884407,0.039884407),
	Colour3f(0,0,0),
	Colour3f(0.37322617,0.37322617,0.37322617),
	Colour3f(0.079864554,0.079864554,0.079864554),
	Colour3f(0.99999994,0.99999994,0.99999994)
};

static bool areMatsDefaultBikeMats(WorldObject* object)
{
	static_assert(staticArrayNumElems(bike_default_cols) == 12);

	if(object->materials.size() != 12)
		return false;

	for(size_t i=0; i<12; ++i)
		if(object->materials[i]->colour_rgb != bike_default_cols[i])
			return false;
	return true;
}

// From GUIClient::summonHovercar
static const Colour3f hovercar_default_cols[] = {
	Colour3f(0.5866589,0.5866589,0.5866589),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.99999994,0.99999994,0.99999994)
};

static bool areMatsDefaultHovercarMats(WorldObject* object)
{
	static_assert(staticArrayNumElems(hovercar_default_cols) == 4);

	if(object->materials.size() != 4)
		return false;

	for(size_t i=0; i<4; ++i)
		if(object->materials[i]->colour_rgb != hovercar_default_cols[i])
			return false;
	return true;
}


static const Colour3f boat_default_cols[] = {
	Colour3f(0.4, 0.88, 1),
	Colour3f(0.98f, 0.98f, 0.98f)
};


static bool areMatsDefaultBoatMats(WorldObject* object)
{
	static_assert(staticArrayNumElems(boat_default_cols) == 2);

	if(object->materials.size() != 2)
		return false;

	for(size_t i=0; i<2; ++i)
		if(object->materials[i]->colour_rgb != boat_default_cols[i])
			return false;

	if(object->materials[1]->colour_texture_url != "Image_1_1_8863066029431458469_jpg_8863066029431458469.jpg")
		return false;

	return true;
}


static const Colour3f car_default_cols[] = {
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.3478059,0.3478059,0.3478059),
	Colour3f(0,0,0),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.55740356,0.55740356,0.55740356),
	Colour3f(0.90588003,0.8705888,0.027453305),
	Colour3f(0.7020756,0.7020756,0.7020756),
	Colour3f(0.42477873,0.42477873,0.42477873),
	Colour3f(0.9666595,0.9666595,0.9666595),
	Colour3f(0.9063317,0.9063317,0.9063317),
	Colour3f(0.9063317,0.9063317,0.9063317),
	Colour3f(0.05700936,0.33066273,0.4655913),
	Colour3f(0.31550452,0.3246547,0.36185718),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.07998883,0.07998883,0.07998883),
	Colour3f(0.35677418,0.35677418,0.35677418),
	Colour3f(0.33637717,0.33637717,0.33637717),
	Colour3f(0.49753314,0.5532372,0.6938043),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.76431364,0,0),
	Colour3f(0.9063317,0.9063317,0.9063317),
	Colour3f(0.92781156,0.8240292,0),
	Colour3f(0.1825715,0.1825715,0.1825715),
	Colour3f(0.14829254,0.533323,0),
	Colour3f(0.08505472,0.4036672,0.7399918),
	Colour3f(0.12665671,0.12665671,0.12665671),
	Colour3f(0.93332434,0.5470529,0.13513926),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.9063317,0.9063317,0.9063317),
	Colour3f(0.6061947,0.6061947,0.6061947),
	Colour3f(0.99999994,0.99999994,0.99999994),
	Colour3f(0.99999994,0.99999994,0.99999994)
};


static bool areMatsDefaultCarMats(WorldObject* object)
{
	static_assert(staticArrayNumElems(car_default_cols) == 33);

	if(object->materials.size() != 33)
		return false;

	for(size_t i=0; i<2; ++i)
		if(object->materials[i]->colour_rgb != car_default_cols[i])
			return false;
	
	return true;
}


// Delete all vehicles that haven't been used for a while, and that use the default mesh and materials.
void WorldMaintenance::removeOldVehicles(Reference<ServerAllWorldsState> all_worlds_state)
{
	WorldStateLock lock(all_worlds_state->mutex);

	const TimeStamp timestamp_cutoff(TimeStamp::currentTime().time - 3600 * 24); // 1 day ago: objects with a last-modified older than this can be deleted.

	int num_bikes_deleted = 0;
	int num_hovercars_deleted = 0;
	int num_boats_deleted = 0;
	int num_cars_deleted = 0;

	for(auto it = all_worlds_state->world_states.begin(); it != all_worlds_state->world_states.end(); ++it)
	{
		ServerWorldState* world_state = it->second.ptr();

		ServerWorldState::ObjectMapType& objects = world_state->getObjects(lock);
		for(auto ob_it = objects.begin(); ob_it != objects.end(); ++ob_it)
		{
			WorldObject* object = ob_it->second.ptr();

			if(BitUtils::isBitSet(object->flags, WorldObject::SUMMONED_FLAG) && (object->last_modified_time <= timestamp_cutoff))
			{
				bool delete_ob = false;

				if((object->model_url == "optimized_dressed_fix7_offset4_glb_4474648345850208925.bmesh") && areMatsDefaultBikeMats(object)) // From GUIClient::summonBike()
				{
					conPrint("WorldMaintenance::removeOldVehicles(): Removing bike with UID: " + object->uid.toString() + ". (Last modified: " + object->last_modified_time.timeAgoDescription() + ")");
					num_bikes_deleted++;
					delete_ob = true;
				}

				if((object->model_url == "peugot_closed_glb_2887717763908023194.bmesh") && areMatsDefaultHovercarMats(object)) // From GUIClient::summonHovercar()
				{
					conPrint("WorldMaintenance::removeOldVehicles(): Removing hovercar with UID: " + object->uid.toString() + ". (Last modified: " + object->last_modified_time.timeAgoDescription() + ")");
					num_hovercars_deleted++;
					delete_ob = true;
				}

				if((object->model_url == "poweryacht3_2_glb_17116251394697619807.bmesh") && areMatsDefaultBoatMats(object)) // From GUIClient::summonBoat()
				{
					conPrint("WorldMaintenance::removeOldVehicles(): Removing boat with UID: " + object->uid.toString() + ". (Last modified: " + object->last_modified_time.timeAgoDescription() + ")");
					num_boats_deleted++;
					delete_ob = true;
				}

				if((object->model_url == "deLorean2_0_glb_5923323464955550713.bmesh") && areMatsDefaultCarMats(object)) // From GUIClient::summonCar()
				{
					conPrint("WorldMaintenance::removeOldVehicles(): Removing car with UID: " + object->uid.toString() + ". (Last modified: " + object->last_modified_time.timeAgoDescription() + ")");
					num_cars_deleted++;
					delete_ob = true;
				}

				if(delete_ob)
				{
					// Mark object as dead
					object->state = WorldObject::State_Dead;
					object->from_remote_other_dirty = true; // This is not actually dirty based on a remote client, use this flag anyway.
					world_state->getDirtyFromRemoteObjects(lock).insert(object);

					// Don't need to mark enclosing LOD chunk as dirty as vehicles shouldn't be baked into LOD chunk mesh anyway.
				}
			}
		}
	}

	if((num_bikes_deleted > 0) || (num_hovercars_deleted > 0) || (num_boats_deleted > 0) || (num_cars_deleted > 0))
		conPrint("WorldMaintenance::removeOldVehicles(): removed " + toString(num_bikes_deleted) + " bike(s), " + toString(num_hovercars_deleted) + " hovercar(s), " + 
			toString(num_cars_deleted) + " car(s) and " + toString(num_boats_deleted) + " boat(s).");
}
