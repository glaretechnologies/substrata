/*=====================================================================
ObjectPathController.h
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include "WorldState.h"
#include "PlayerPhysics.h"
#include "PhysicsObject.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>


class CameraController;
class PhysicsWorld;
class ThreadContext;


struct PathWaypointIn
{
	enum WaypointType
	{
		CurveIn,
		CurveOut,
		Station
	};

	//UID waypoint_ob_uid;
	Vec4f pos;
	WaypointType waypoint_type;
};


/*=====================================================================
ObjectPathController
--------------------

=====================================================================*/
class ObjectPathController : public RefCounted
{
public:
	ObjectPathController(WorldState& world_state, WorldObjectRef controlled_ob, const std::vector<PathWaypointIn>& waypoints_in, double initial_time, UID follow_ob_uid, float follow_dist);
	~ObjectPathController();

	void update(WorldState& world_state, PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime);

	static void sortPathControllers(std::vector<Reference<ObjectPathController>>& controllers);
private:
	void walkAlongPathForTime(WorldState& world_state, /*int& waypoint_index, float& dir_along_segment, */double delta_time, Vec4f& pos_out, Vec4f& dir_out, Vec4f& target_pos_out, double& target_dtime_out);
	void walkAlongPathDistBackwards(WorldState& world_state, int waypoint_index, float dir_along_segment, float delta_dist, /*int& waypoint_index_out, float& dir_along_segment_out, */Vec4f& pos_out, Vec4f& dir_out);
	float getSegmentLength(WorldState& world_state, int waypoint_index);
public:
	WorldObjectRef controlled_ob;

	int cur_waypoint_index; // Index of the waypoint we are at or have just passed.

	float m_dist_along_segment;
	float m_time_along_segment;

	UID follow_ob_uid;
	float follow_dist;

	struct PathWaypoint
	{
		Vec4f pos;
		PathWaypointIn::WaypointType waypoint_type;
	};
private:
	std::vector<PathWaypoint> waypoints;
};
