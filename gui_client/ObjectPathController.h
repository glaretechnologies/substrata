/*=====================================================================
ObjectPathController.h
----------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "WorldState.h"
#include "PlayerPhysics.h"
#include "PhysicsObject.h"
#include "../shared/WorldObject.h"
#include <maths/Vec4f.h>
#include <maths/vec3.h>
#include <vector>


class PhysicsWorld;


struct PathWaypointIn
{
	enum WaypointType
	{
		CurveIn, // Start of a curve (circular arc)
		CurveOut, // End of a curve
		Station // Vertex on the path.  The object will pause here for 'pause_time' seconds.
	};

	PathWaypointIn() : pause_time(10.f), speed(10.f) {} 
	PathWaypointIn(const Vec4f& pos_, WaypointType waypoint_type_): pos(pos_), waypoint_type(waypoint_type_) {}

	Vec4f pos;
	WaypointType waypoint_type;
	float pause_time;
	float speed; // speed to next waypoint
};


/*=====================================================================
ObjectPathController
--------------------
Code to make an object move along a path.
The path is defined by the user in an XML script. (see Scripting class)
=====================================================================*/
class ObjectPathController : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	ObjectPathController(WorldObjectRef controlled_ob, const std::vector<PathWaypointIn>& waypoints_in, double initial_time, UID follow_ob_uid, float follow_dist, bool orient_along_path);
	~ObjectPathController();

	void update(WorldState& world_state, PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime) REQUIRES(world_state.mutex);

	Vec4f evalSegmentCurvePos(int waypoint_index, float frac) const;

	static void sortPathControllers(std::vector<Reference<ObjectPathController>>& controllers);
private:
	void walkAlongPathForTime(double delta_time, Vec4f& pos_out, Vec4f& dir_out);
	void evalAlongPathDistBackwards(int waypoint_index, float dir_along_segment, float delta_dist, Vec4f& pos_out, Vec4f& dir_out) const;
	inline Vec4f waypointPos(int waypoint_index) const;
public:
	WorldObjectRef controlled_ob;

	int cur_waypoint_index; // Index of the waypoint we are at or have just passed.

	float m_dist_along_segment; // Current distance we have travelled along the current segment.
	float m_time_along_segment; // Current duration we have travelled along the current segment.  Note that some of this time can have been spent waiting at a station at the start of the segment.

	UID follow_ob_uid;
	float follow_dist;

	bool orient_along_path; // Should we rotate the object to point along the path?

	struct PathWaypoint
	{
		GLARE_ALIGNED_16_NEW_DELETE

		Vec4f pos;
		PathWaypointIn::WaypointType waypoint_type;
		float pause_time;
		float speed;
		float segment_len; // length along segment to next waypoint
		float just_curve_len; // length along just the curve.
		float entry_segment_len; // length of straight part before curve (if any)
		float curve_angle;
		float curve_r;
	};
	std::vector<PathWaypoint> waypoints;
};
