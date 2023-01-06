/*=====================================================================
ObjectPathController.cpp
------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "ObjectPathController.h"


#include "CameraController.h"
#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <TopologicalSort.h>


ObjectPathController::ObjectPathController(WorldObjectRef controlled_ob_, const std::vector<PathWaypointIn>& waypoints_in, double initial_time, UID follow_ob_uid_, float follow_dist_)
{
	cur_waypoint_index = 0;
	m_dist_along_segment = 0;
	m_time_along_segment = 0;
	controlled_ob = controlled_ob_;
	follow_ob_uid = follow_ob_uid_;
	follow_dist = follow_dist_;

	waypoints.resize(waypoints_in.size());

	for(size_t i=0; i<waypoints.size(); ++i)
	{
		waypoints[i].pos = waypoints_in[i].pos;
		waypoints[i].waypoint_type = waypoints_in[i].waypoint_type;
		waypoints[i].pause_time = waypoints_in[i].pause_time;
		waypoints[i].speed = waypoints_in[i].speed;
	}

	// Get total path traversal time
	double total_time = 0;
	for(size_t i=0; i != waypoints.size(); ++i)
	{
		const double seg_len = getSegmentLength((int)i);
		if(seg_len < 1.0e-5)
			throw glare::Exception("Invalid path, near zero length segment");

		const double seg_traversal_time = seg_len / waypoints[i].speed;
		total_time += seg_traversal_time;

		if(waypoints[i].waypoint_type == PathWaypointIn::Station)
			total_time += waypoints[i].pause_time;
	}
	
	// Get initial time mod total path traversal time
	initial_time = Maths::doubleMod(initial_time, total_time);

	// Advance to initial state
	Vec4f new_pos, new_dir;
	Vec4f target_pos;
	double target_dtime;
	walkAlongPathForTime(initial_time, new_pos, new_dir, target_pos, target_dtime);
}


ObjectPathController::~ObjectPathController()
{

}


inline static Vec4f getWaypointPos(ObjectPathController::PathWaypoint& waypoint)
{
	return waypoint.pos;
}


// Get length of path segment from waypoint waypoint_index to (waypoint_index + 1) % num waypoints.
float ObjectPathController::getSegmentLength(int waypoint_index)
{
	const Vec4f entry_pos = getWaypointPos(waypoints[waypoint_index]);
	const Vec4f exit_pos  = getWaypointPos(waypoints[Maths::intMod(waypoint_index + 1, (int)waypoints.size())]);

	const bool curve = waypoints[waypoint_index].waypoint_type == PathWaypointIn::CurveIn;
	if(curve)
	{
		// Assume 90 degree angle.
		// r^2 + r^2 = d^2  =>   2r^2 = d^2     =>    r^2 = d^2 / 2   =>      r = sqrt(d^2/2)
		const float curve_r = std::sqrt(entry_pos.getDist2(exit_pos) / 2.f);
		const float curve_len = Maths::pi_2<float>() * curve_r;
		return curve_len;
	}
	else
	{
		return entry_pos.getDist(exit_pos);
	}
}


void ObjectPathController::walkAlongPathDistBackwards(int waypoint_index, float dir_along_segment, float delta_dist, /*int& waypoint_index_out, float& dir_along_segment_out, */Vec4f& pos_out, Vec4f& dir_out)
{
	while(delta_dist > 0)
	{
		const bool on_curve = waypoints[waypoint_index].waypoint_type == PathWaypointIn::CurveIn;

		const Vec4f entry_pos = getWaypointPos(waypoints[waypoint_index]);
		const Vec4f exit_pos  = getWaypointPos(waypoints[Maths::intMod(waypoint_index + 1, (int)waypoints.size())]);

		if(on_curve)
		{
			const Vec4f entry_dir = normalise(entry_pos - getWaypointPos(waypoints[Maths::intMod(waypoint_index - 1, (int)waypoints.size())]));
			const Vec4f exit_dir  = normalise(getWaypointPos(waypoints[Maths::intMod(waypoint_index + 2, (int)waypoints.size())]) - exit_pos);

			//conPrint("entry_dir: " + entry_dir.toStringNSigFigs(4));
			//conPrint("exit_dir: " + exit_dir.toStringNSigFigs(4));

			// r^2 + r^2 = d^2  =>   2r^2 = d^2     =>    r^2 = d^2 / 2   =>      r = sqrt(d^2/2)
			const float curve_r = std::sqrt(entry_pos.getDist2(exit_pos) / 2.f);
			const float curve_len = Maths::pi_2<float>() * curve_r;

			//printVar(curve_r);
			//printVar(curve_len);

			const float curve_len_remaining = dir_along_segment; // curve_len - dir_along_segment;
			if(delta_dist < curve_len_remaining)
			{
				dir_along_segment -= delta_dist;
				delta_dist = 0;

				const float angle = (dir_along_segment / curve_len) * Maths::pi_2<float>();
				//printVar(angle);
				pos_out = entry_pos + entry_dir * std::sin(angle) * curve_r + exit_dir * (curve_r - std::cos(angle) * curve_r);
				dir_out = entry_dir * cos(angle) + exit_dir * sin(angle);
				break;
			}
			else // else if we travel past this curve backwards:
			{
				delta_dist -= curve_len_remaining;
				waypoint_index = Maths::intMod(waypoint_index - 1, (int)waypoints.size()); // Advance (backwards) to next waypoint
				dir_along_segment = getSegmentLength(waypoint_index); // We are now at the end of the previous waypoint.
			}
		}
		else
		{
			const float segment_len = entry_pos.getDist(exit_pos);
			const float segment_len_remaining = dir_along_segment;//segment_len - dir_along_segment;
			if(delta_dist < segment_len_remaining)
			{
				dir_along_segment -= delta_dist;
				delta_dist = 0;

				pos_out = Maths::lerp(entry_pos, exit_pos, dir_along_segment / segment_len);
				dir_out = normalise(exit_pos - entry_pos);
				break;
			}
			else // else if we travel past this segment backwards:
			{
				delta_dist -= segment_len_remaining;
				waypoint_index = Maths::intMod(waypoint_index - 1, (int)waypoints.size()); // Advance (backwards) to next waypoint
				dir_along_segment = getSegmentLength(waypoint_index); // We are now at the end of the previous waypoint.
			}
		}
	}
}



void ObjectPathController::walkAlongPathForTime(/*int& waypoint_index, float& dir_along_segment, */double delta_time, Vec4f& pos_out, Vec4f& dir_out, Vec4f& target_pos_out, double& target_dtime_out)
{
	// Walk forwards by dt
	float dtime_remaining = (float)delta_time;
	while(dtime_remaining > 0)
	{
		const bool on_curve = waypoints[cur_waypoint_index].waypoint_type == PathWaypointIn::CurveIn;

		const Vec4f entry_pos = getWaypointPos(waypoints[cur_waypoint_index]);
		const Vec4f exit_pos  = getWaypointPos(waypoints[Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size())]);

		const Vec4f begin_dir = normalise(getWaypointPos(waypoints[cur_waypoint_index]) - getWaypointPos(waypoints[Maths::intMod(cur_waypoint_index - 1, (int)waypoints.size())]));
		const Vec4f end_dir = normalise(exit_pos - entry_pos);

		const float vel = waypoints[cur_waypoint_index].speed;
		if(on_curve)
		{
			// r^2 + r^2 = d^2  =>   2r^2 = d^2     =>    r^2 = d^2 / 2   =>      r = sqrt(d^2/2)
			const float curve_r = std::sqrt(entry_pos.getDist2(exit_pos) / 2.f);
			const float curve_len = Maths::pi_2<float>() * curve_r;

			const float curve_traversal_time = curve_len / vel;

			float curve_time_remaining = curve_traversal_time - m_time_along_segment;

			if(dtime_remaining < curve_time_remaining) // If we won't reach the end of the curve in the dtime remaining:
			{
				//curve_time_remaining -= dtime_remaining;
				m_dist_along_segment += dtime_remaining * vel;
				m_time_along_segment += dtime_remaining;
				dtime_remaining = 0;

				const Vec4f entry_dir = normalise(entry_pos - getWaypointPos(waypoints[Maths::intMod(cur_waypoint_index - 1, (int)waypoints.size())]));
				const Vec4f exit_dir  = normalise(getWaypointPos(waypoints[Maths::intMod(cur_waypoint_index + 2, (int)waypoints.size())]) - exit_pos);

				//conPrint("entry_dir: " + entry_dir.toStringNSigFigs(4));
				//conPrint("exit_dir: " + exit_dir.toStringNSigFigs(4));

				const float angle = (m_dist_along_segment / curve_len) * Maths::pi_2<float>();
				pos_out = entry_pos + entry_dir * std::sin(angle) * curve_r + exit_dir * (curve_r - std::cos(angle) * curve_r);
				dir_out = entry_dir * cos(angle) + exit_dir * sin(angle);
				target_pos_out = pos_out;
				target_dtime_out = delta_time;
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= curve_time_remaining;

				// Advance to next segment
				m_dist_along_segment = 0;
				m_time_along_segment = 0;
				cur_waypoint_index = Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size());
			}
		}
		else if(waypoints[cur_waypoint_index].waypoint_type == PathWaypointIn::Station)
		{
			if(m_time_along_segment < waypoints[cur_waypoint_index].pause_time) // If still stopped at station:
			{
				const float stop_time_remaining = waypoints[cur_waypoint_index].pause_time - m_time_along_segment;

				if(dtime_remaining < stop_time_remaining) // If we will still be stopped in the dtime remaining:
				{
					m_time_along_segment += dtime_remaining;
					dtime_remaining = 0;

					pos_out = entry_pos;
					dir_out = begin_dir; // normalise(exit_pos - entry_pos);
					target_pos_out = entry_pos;
					target_dtime_out = stop_time_remaining - dtime_remaining;
					break;
				}
				else // else if we start moving again in dtime:
				{
					dtime_remaining -= stop_time_remaining;
					m_time_along_segment += stop_time_remaining;
				}
			}

			const float segment_len = entry_pos.getDist(exit_pos);
			const float segment_traversal_time = segment_len / vel;
			const float travelling_time_along_segment = m_time_along_segment - waypoints[cur_waypoint_index].pause_time; // time spent travelling past the station so far

			float segment_time_remaining = segment_traversal_time - travelling_time_along_segment;
			if(dtime_remaining < segment_time_remaining) // If we won't reach the end of the segment in the dtime remaining:
			{
				m_dist_along_segment += dtime_remaining * vel;
				m_time_along_segment += dtime_remaining;
				dtime_remaining = 0;

				const float segment_dist_frac = m_dist_along_segment / segment_len;
				pos_out = Maths::uncheckedLerp(entry_pos, exit_pos, segment_dist_frac);
				dir_out = Maths::uncheckedLerp(begin_dir, end_dir, segment_dist_frac); // normalise(exit_pos - entry_pos);
				target_pos_out = exit_pos;
				target_dtime_out = segment_time_remaining - dtime_remaining;
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= segment_time_remaining;

				// Advance to next segment
				m_dist_along_segment = 0;
				m_time_along_segment = 0;
				cur_waypoint_index = Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size());
			}
		}
		else // else if waypoint_type == PathWaypointIn::CurveOut:
		{
			assert(waypoints[cur_waypoint_index].waypoint_type == PathWaypointIn::CurveOut);

			const float segment_len = entry_pos.getDist(exit_pos);
			const float segment_traversal_time = segment_len / vel;
			float segment_time_remaining = segment_traversal_time - m_time_along_segment;
			if(dtime_remaining < segment_time_remaining) // If we won't reach the end of the segment in the dtime remaining:
			{
				//curve_time_remaining -= dtime_remaining;
				m_dist_along_segment += dtime_remaining * vel;
				m_time_along_segment += dtime_remaining;
				dtime_remaining = 0;

				pos_out = Maths::uncheckedLerp(entry_pos, exit_pos, m_dist_along_segment / segment_len);
				dir_out = normalise(exit_pos - entry_pos);
				target_pos_out = exit_pos;
				target_dtime_out = segment_time_remaining - dtime_remaining;
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= segment_time_remaining;

				// Advance to next segment
				m_dist_along_segment = 0;
				m_time_along_segment = 0;
				cur_waypoint_index = Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size());
			}
		}
	}
}


void ObjectPathController::update(WorldState& world_state, PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime)
{
	/*for(size_t i=0; i<waypoints.size(); ++i)
	{
		conPrint("waypoints " + leftPad(toString(i), ' ', 2) + ": UID " + waypoints[i].waypoint_ob_uid.toString() + ": " + getWaypointPos(world_state, waypoints[i]).toStringNSigFigs(6));
	}*/

	Vec4f new_pos;
	Vec4f new_dir;
	Vec4f target_pos;
	double target_dtime = -1;

	if(follow_ob_uid.valid())
	{
		// If we are following another object:

		auto res = world_state.objects.find(follow_ob_uid);
		if(res != world_state.objects.end())
		{
			WorldObjectRef lead_ob = res.getValue();

			// get leading ob waypoint index and dir_along_segment.
			// Walk back along the path by the following distance.

			const int leader_waypoint_index = lead_ob->waypoint_index;
			const float leader_dist_along_segment = lead_ob->dist_along_segment;

			walkAlongPathDistBackwards(leader_waypoint_index, leader_dist_along_segment, follow_dist, /*pos_out=*/new_pos, /*dir_out=*/new_dir);
		}
		else
		{
			new_pos = Vec4f(0,0,0,1);
			new_dir = Vec4f(1,0,0,0);
		}
	}
	else
	{

		walkAlongPathForTime(dtime, new_pos, new_dir, target_pos, target_dtime);

		controlled_ob->waypoint_index = cur_waypoint_index;
		controlled_ob->dist_along_segment = m_dist_along_segment;
	}

	const Quatf initial_ob_rot = Quatf::fromAxisAndAngle(normalise(controlled_ob->axis), controlled_ob->angle);

	const float track_angle = std::atan2(new_dir[1], new_dir[0]);

	const Quatf track_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), track_angle);

	const Quatf final_rot_quat = track_rot * initial_ob_rot;
	Vec4f axis;
	float angle;
	final_rot_quat.toAxisAndAngle(axis, angle);
	
	// Update OpenGL object transform directly:
	//if(controlled_ob->opengl_engine_ob.nonNull())
	//{
	//	controlled_ob->opengl_engine_ob->ob_to_world_matrix = obToWorldMatrix(*controlled_ob);
	//
	//	opengl_engine->updateObjectTransformData(*controlled_ob->opengl_engine_ob);
	//
	//	if(controlled_ob->opengl_engine_ob->mesh_data.nonNull())
	//		controlled_ob->aabb_ws = controlled_ob->opengl_engine_ob->mesh_data->aabb_os.transformedAABBFast(controlled_ob->opengl_engine_ob->ob_to_world_matrix);
	//	//conPrint("train AABBWS: " + controlled_ob->aabb_ws.toStringNSigFigs(3));	
	//}

	// Update in physics engine
	if(controlled_ob->physics_object.nonNull())
	{
		//if(target_dtime >= 0)
		//{
		//	conPrint("Moving to " + target_pos.toStringMaxNDecimalPlaces(3) + " in dtime " + ::doubleToStringMaxNDecimalPlaces(target_dtime, 3) + " s");
		//	physics_world.moveKinematicObject(*controlled_ob->physics_object, target_pos, final_rot_quat, target_dtime);
		//}
		//else
		{
			physics_world.moveKinematicObject(*controlled_ob->physics_object, new_pos, final_rot_quat, dtime);
		}
	}
}


void ObjectPathController::sortPathControllers(std::vector<Reference<ObjectPathController>>& path_controllers)
{
	// Build follows data
	std::vector<std::pair<Reference<ObjectPathController>, Reference<ObjectPathController>>> follows_pairs;

	for(size_t i=0; i<path_controllers.size(); ++i)
	{
		if(path_controllers[i]->follow_ob_uid.valid())
		{
			for(size_t z=0; z<path_controllers.size(); ++z)
				if(path_controllers[z]->controlled_ob->uid == path_controllers[i]->follow_ob_uid) // If controller i follows controller z: (follow edge from i to z)
				{
					follows_pairs.push_back(std::make_pair(path_controllers[i], path_controllers[z])); // Add follows edge from i to z.
				}
		}
	}

	/*const bool sorted =*/ TopologicalSort::topologicalSort(path_controllers.begin(), path_controllers.end(), follows_pairs);
}
