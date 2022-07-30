/*=====================================================================
ProximityLoader.h
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "HashedObGrid.h"
#include "../opengl/OpenGLEngine.h"
#include "../shared/WorldObject.h"
#include <string>
#include <unordered_set>


class ObLoadingCallbacks
{
public:
	virtual void loadObject(WorldObjectRef ob) = 0;

	virtual void unloadObject(WorldObjectRef ob) = 0;

	virtual void newCellInProximity(const Vec3<int>& cell_coords) = 0;
};


/*=====================================================================
ProximityLoader
---------------
Loads or unloads the graphics and physics of objects depending on how close the camera is to them.
Does the loading/unloading by calling callback functions - 
when the camera moves close to an object, the loadObject() callback is called,
when it moves away from an object, the unloadObject() callback is called.

When the camera moves close to a new grid cell, calls the newCellInProximity() callback.
This allows MainWindow to send a QueryObjects message to the server.
=====================================================================*/
class ProximityLoader
{
public:
	ProximityLoader(float load_distance);
	~ProximityLoader();

	void setLoadDistance(float new_load_distance);
	float getLoadDistance() const { return load_distance; }

	void checkAddObject(WorldObjectRef ob); // Add object it not already added
	void removeObject(WorldObjectRef ob);

	void clearAllObjects();

	//inline bool isObjectInLoadProximity(const WorldObject* ob)
	//{
	//	const float ob_load_dist2 = myMin(ob->max_load_dist2, load_distance2);
	//	return ob->pos.toVec4fPoint().getDist2(last_cam_pos) <= ob_load_dist2;
	//}

	// Notify the ProximityLoader that an object has changed position
	void objectTransformChanged(WorldObject* ob);

	// Notify the ProximityLoader that the camera has moved
	void updateCamPos(const Vec4f& new_cam_pos);

	// Sets initial camera position, doesn't issue load object callbacks (assumes no objects downloaded yet)
	// Returns query AABB
	js::AABBox setCameraPosForNewConnection(const Vec4f& initial_cam_pos);

	//----------------------------------- Diagnostics ----------------------------------------
	std::string getDiagnostics() const;
	//----------------------------------------------------------------------------------------

	static void test();


	ObLoadingCallbacks* callbacks;

	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;

	float load_distance;
	float load_distance2;
	HashedObGrid ob_grid;
	Vec4f last_cam_pos;
};
