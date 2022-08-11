/*=====================================================================
WorldCreation.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"


/*=====================================================================
WorldCreation
-------------
Parcel layout and creation, road layout and creation etc.
=====================================================================*/
class WorldCreation
{
public:
	static void createParcelsAndRoads(Reference<ServerAllWorldsState> world_state);
};
