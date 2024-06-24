/*=====================================================================
ScriptedObjectProximityChecker.h
--------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "HashedObGrid.h"
#include "../utils/LinearIterSet.h"
#include "../shared/WorldObject.h"
class GUIClient;


/*=====================================================================
ScriptedObjectProximityChecker
------------------------------

=====================================================================*/
class ScriptedObjectProximityChecker
{
public:
	ScriptedObjectProximityChecker();
	~ScriptedObjectProximityChecker();

	void addObject(WorldObjectRef ob) { objects.insert(ob); }
	void removeObject(WorldObjectRef ob) { objects.erase(ob); }
	void clear();

	void think(const Vec4f& campos);

	glare::LinearIterSet<WorldObjectRef, WorldObjectRefHash> objects;

	GUIClient* gui_client;
};
