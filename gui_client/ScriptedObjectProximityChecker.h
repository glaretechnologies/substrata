/*=====================================================================
ScriptedObjectProximityChecker.h
--------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../utils/LinearIterSet.h"
#include "../shared/WorldObject.h"
class GUIClient;


/*=====================================================================
ScriptedObjectProximityChecker
------------------------------
Stores references to all objects that have a script that has one of the 
spatial event handlers, for example onUserMovedNearToObject.

Currently just uses linear iteration over such objects but could use some kind of
spatial acceleration data structure in the future.
=====================================================================*/
class ScriptedObjectProximityChecker
{
public:
	ScriptedObjectProximityChecker();
	~ScriptedObjectProximityChecker();

	void addObject(WorldObjectRef ob) { objects.insert(ob); }
	void removeObject(WorldObjectRef ob) { objects.erase(ob); }
	void clear();

	// Iterate over objects, see if the player has moved near to or away from any objects, and execute the relevant event handlers if so.
	void think(const Vec4f& campos);

	glare::LinearIterSet<WorldObjectRef, WorldObjectRefHash> objects;

	GUIClient* gui_client;
};
