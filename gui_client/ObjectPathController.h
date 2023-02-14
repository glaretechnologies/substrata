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

	PathWaypointIn() : pause_time(10.f), speed(10.f) {} 
	PathWaypointIn(const Vec4f& pos_, WaypointType waypoint_type_): pos(pos_), waypoint_type(waypoint_type_) {}

	//UID waypoint_ob_uid;
	Vec4f pos;
	WaypointType waypoint_type;
	float pause_time;
	float speed; // speed to next waypoint
};


/*=====================================================================
ObjectPathController
--------------------

=====================================================================*/
class ObjectPathController : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	ObjectPathController(WorldObjectRef controlled_ob, const std::vector<PathWaypointIn>& waypoints_in, double initial_time, UID follow_ob_uid, float follow_dist);
	~ObjectPathController();

	void update(WorldState& world_state, PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime) REQUIRES(world_state.mutex);

	static void sortPathControllers(std::vector<Reference<ObjectPathController>>& controllers);
private:
	void walkAlongPathForTime(/*int& waypoint_index, float& dir_along_segment, */double delta_time, Vec4f& pos_out, Vec4f& dir_out, Vec4f& target_pos_out, double& target_dtime_out);
	void walkAlongPathDistBackwards(int waypoint_index, float dir_along_segment, float delta_dist, /*int& waypoint_index_out, float& dir_along_segment_out, */Vec4f& pos_out, Vec4f& dir_out);
	float getSegmentLength(int waypoint_index);
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
		float pause_time;
		float speed;
	};
	std::vector<PathWaypoint> waypoints;
};
