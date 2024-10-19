/*=====================================================================
WorldMaintenance.h
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"


/*=====================================================================
WorldMaintenance
----------------
=====================================================================*/
class WorldMaintenance
{
public:
	static void removeOldVehicles(Reference<ServerAllWorldsState> world_state);
};
