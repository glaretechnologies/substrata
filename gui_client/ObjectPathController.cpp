/*=====================================================================
ObjectPathController.cpp
------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "ObjectPathController.h"


#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/TopologicalSort.h>


// See https://math.stackexchange.com/questions/1036959/midpoint-of-the-shortest-distance-between-2-rays-in-3d
// In particular this answer: https://math.stackexchange.com/a/2371053
static inline Vec4f closestPointOnLineToRay(const Vec4f& line_orig, const Vec4f& line_dir, const Vec4f& origin, const Vec4f& unitdir)
{
	const Vec4f a = line_orig;
	const Vec4f b = line_dir;

	const Vec4f c = origin;
	const Vec4f d = unitdir;

	const float t = (dot(c - a, b) + dot(a - c, d) * dot(b, d)) / (1 - Maths::square(dot(b, d)));

	return a + b * t;
}


ObjectPathController::ObjectPathController(WorldObjectRef controlled_ob_, const std::vector<PathWaypointIn>& waypoints_in, double initial_time, UID follow_ob_uid_, float follow_dist_, bool orient_along_path_)
{
	cur_waypoint_index = 0;
	m_dist_along_segment = 0;
	m_time_along_segment = 0;
	controlled_ob = controlled_ob_;
	follow_ob_uid = follow_ob_uid_;
	follow_dist = follow_dist_;
	orient_along_path = orient_along_path_;

	waypoints.resize(waypoints_in.size());

	for(size_t i=0; i<waypoints.size(); ++i)
	{
		waypoints[i].pos = waypoints_in[i].pos;
		waypoints[i].waypoint_type = waypoints_in[i].waypoint_type;
		waypoints[i].pause_time = waypoints_in[i].pause_time;
		waypoints[i].speed = waypoints_in[i].speed;
	}

	if(waypoints.size() < 2)
		throw glare::Exception("Invalid path, there must be at least 2 waypoints");

	if(follow_dist < 0.f)
		throw glare::Exception("follow_dist must be >= 0");

	// Compute segment info and get total path traversal time
	double total_time = 0;
	for(int i=0; i < (int)waypoints.size(); ++i)
	{
		// Compute segment info
		const Vec4f entry_pos = waypointPos(i);
		const Vec4f exit_pos  = waypointPos(i + 1);
		const Vec4f entry_dir = normalise(entry_pos - waypointPos(i - 1));
		const Vec4f exit_dir  = normalise(waypointPos(i + 2) - exit_pos);

		if(entry_pos.getDist(exit_pos) < 1.0e-5f)
			throw glare::Exception("Invalid path, near zero length segment");

		if(waypoints[i].waypoint_type == PathWaypointIn::CurveIn)
		{
			const float angle = std::acos(myClamp(dot(entry_dir, exit_dir), -1.f, 1.f));

			if(angle < 1.0e-6f) // Entry and exit direction of curve are equal.  Can't really treat as a circle arc, just treat as an entry segment.
			{
				//throw glare::Exception("Entry direction to curve and exit direction from curve cannot be the same.");
				waypoints[i].segment_len = entry_pos.getDist(exit_pos);
				waypoints[i].just_curve_len = 0;
				waypoints[i].entry_segment_len = entry_pos.getDist(exit_pos);
				waypoints[i].curve_angle  = 0;
				waypoints[i].curve_r  = 1000.f;
				waypoints[i].exit_segment_unit_dir = normalise(exit_pos - entry_pos);
			}
			else
			{
				const Vec4f dir_intersect_p = closestPointOnLineToRay(entry_pos, entry_dir, exit_pos, exit_dir);

				const float entry_pos_to_intersect_p_dist = dir_intersect_p.getDist(entry_pos);
				const float exit_pos_to_intersect_p_dist  = dir_intersect_p.getDist(exit_pos);
				const float d = myMin(entry_pos_to_intersect_p_dist, exit_pos_to_intersect_p_dist);
				const float curve_r = d / std::tan(angle / 2.f);

				const float curve_len = angle * curve_r;

				const float entry_segment_len = myMax(entry_pos_to_intersect_p_dist - d, 0.0f);
				const float exit_segment_len  = myMax(exit_pos_to_intersect_p_dist  - d, 0.0f);
		
				waypoints[i].segment_len = curve_len + entry_segment_len + exit_segment_len;
				waypoints[i].just_curve_len = curve_len;
				waypoints[i].entry_segment_len = entry_segment_len;
				waypoints[i].curve_angle  = angle;
				waypoints[i].curve_r  = curve_r;
				waypoints[i].exit_segment_unit_dir = normalise(exit_pos - dir_intersect_p);
			}
		}
		else
		{
			waypoints[i].segment_len = entry_pos.getDist(exit_pos);
		}

		if(!isFinite(waypoints[i].segment_len) || waypoints[i].segment_len < 1.0e-5f)
			throw glare::Exception("Invalid path, near zero length segment");

		assert(isFinite(waypoints[i].segment_len));

		const double seg_traversal_time = waypoints[i].segment_len / waypoints[i].speed;
		total_time += seg_traversal_time;

		if(waypoints[i].waypoint_type == PathWaypointIn::Station)
			total_time += waypoints[i].pause_time;
	}
	
	// Get initial time mod total path traversal time
	initial_time = Maths::doubleMod(initial_time, total_time);

	// Advance to initial state
	Vec4f new_pos, new_dir;
	walkAlongPathForTime(initial_time, new_pos, new_dir);
}


ObjectPathController::~ObjectPathController()
{}


inline Vec4f ObjectPathController::waypointPos(int waypoint_index) const
{
	return waypoints[Maths::intMod(waypoint_index, (int)waypoints.size())].pos;
}


// Starting at the point of distance dist_along_segment from waypoint_index towards waypoint_index + 1,
// walk back delta_dist, and compute the position and direction on the path there.
void ObjectPathController::evalAlongPathDistBackwards(int waypoint_index, float dist_along_segment, float delta_dist, Vec4f& pos_out, Vec4f& dir_out) const
{
	while(delta_dist >= 0)
	{
		const Vec4f entry_pos = waypointPos(waypoint_index);
		const Vec4f exit_pos  = waypointPos(waypoint_index + 1);

		const Vec4f entry_dir = normalise(entry_pos - waypointPos(waypoint_index - 1));
		const Vec4f exit_dir  = normalise(waypointPos(waypoint_index + 2) - exit_pos);

		if(waypoints[waypoint_index].waypoint_type == PathWaypointIn::CurveIn)
		{
			const float curve_len_remaining = dist_along_segment;

			if(delta_dist < curve_len_remaining) // If we won't reach the start of the curve in delta_dist:
			{
				const float entry_segment_len = waypoints[waypoint_index].entry_segment_len;
				const float just_curve_len    = waypoints[waypoint_index].just_curve_len;
				const float total_segment_len = waypoints[waypoint_index].segment_len;

				dist_along_segment -= delta_dist;
				delta_dist = 0;

				if(dist_along_segment < entry_segment_len) // If we end up in entry segment:
				{
					pos_out = entry_pos + entry_dir * dist_along_segment;
					dir_out = entry_dir;
				}
				else if(dist_along_segment < entry_segment_len + just_curve_len) // If we won't reach the end of the curve:
				{
					const float curve_frac = (dist_along_segment - entry_segment_len) / just_curve_len; // Fraction along curve

					const Vec4f orthog_exit_dir = normalise(::removeComponentInDir(waypoints[waypoint_index].exit_segment_unit_dir, entry_dir)); // TODO: handle parallel or antiparallel begin_dir and exit_dir.
					const float angle = curve_frac * waypoints[waypoint_index].curve_angle;
					const float curve_r     = waypoints[waypoint_index].curve_r;
					
					pos_out = entry_pos + entry_dir * entry_segment_len         +     (entry_dir * curve_r * std::sin(angle)   +    orthog_exit_dir * curve_r * (1 - std::cos(angle)));
					dir_out = entry_dir * cos(angle) + orthog_exit_dir * sin(angle);
				}
				else // Else we will end up on the exit segment (the straight line after the curve)
				{
					const float dist_uncovered = total_segment_len - dist_along_segment;

					pos_out = exit_pos - waypoints[waypoint_index].exit_segment_unit_dir * dist_uncovered;
					dir_out = waypoints[waypoint_index].exit_segment_unit_dir;
				}
				break;
			}
			else // else if we travel past this curve backwards:
			{
				delta_dist -= curve_len_remaining;
				waypoint_index = Maths::intMod(waypoint_index - 1, (int)waypoints.size()); // Advance (backwards) to next waypoint
				dist_along_segment = waypoints[waypoint_index].segment_len; // We are now at the end of the previous waypoint.
			}
		}
		else
		{
			const float segment_len = waypoints[waypoint_index].segment_len;
			const float segment_len_remaining = dist_along_segment;
			if(delta_dist < segment_len_remaining)
			{
				dist_along_segment -= delta_dist;
				delta_dist = 0;

				const float segment_dist_frac = dist_along_segment / segment_len;
				pos_out = Maths::lerp(entry_pos, exit_pos, segment_dist_frac);
				const Vec4f end_dir = normalise(exit_pos - entry_pos);

				if(waypoints[waypoint_index].waypoint_type == PathWaypointIn::CurveOut)
					dir_out = normalise(exit_pos - entry_pos);
				else
					dir_out = Maths::uncheckedLerp(entry_dir, end_dir, Maths::smoothStep(0.f, 1.f, segment_dist_frac + 0.5f)*2.f - 1.f);
				break;
			}
			else // else if we travel past this segment backwards:
			{
				delta_dist -= segment_len_remaining;
				waypoint_index = Maths::intMod(waypoint_index - 1, (int)waypoints.size()); // Advance (backwards) to next waypoint
				dist_along_segment = waypoints[waypoint_index].segment_len; // We are now at the end of the previous waypoint.
			}
		}
	}
}



void ObjectPathController::walkAlongPathForTime(double delta_time, Vec4f& pos_out, Vec4f& dir_out)
{
	// Walk forwards by delta_time
	float dtime_remaining = (float)delta_time;
	while(dtime_remaining > 0)
	{
		const Vec4f entry_pos = waypointPos(cur_waypoint_index);
		const Vec4f exit_pos  = waypointPos(cur_waypoint_index + 1);

		const Vec4f begin_dir = normalise(entry_pos - waypointPos(cur_waypoint_index - 1));

		const float vel = waypoints[cur_waypoint_index].speed;
		if(waypoints[cur_waypoint_index].waypoint_type == PathWaypointIn::CurveIn)
		{
			const float curve_len = waypoints[cur_waypoint_index].segment_len;
			const float curve_traversal_time = curve_len / vel;
			
			float curve_time_remaining = curve_traversal_time - m_time_along_segment;

			if(dtime_remaining < curve_time_remaining) // If we won't reach the end of the curve in the dtime remaining:
			{
				const float entry_segment_len = waypoints[cur_waypoint_index].entry_segment_len;
				const float just_curve_len    = waypoints[cur_waypoint_index].just_curve_len;
				float entry_segment_time_remaining    = entry_segment_len                    / vel - m_time_along_segment;
				float entry_plus_curve_time_remaining = (entry_segment_len + just_curve_len) / vel - m_time_along_segment;

				if(dtime_remaining < entry_segment_time_remaining) // If we won't reach the end of the entry segment in the dtime remaining:
				{
					m_dist_along_segment += dtime_remaining * vel;
					m_time_along_segment += dtime_remaining;

					pos_out = entry_pos + begin_dir * m_dist_along_segment;
					dir_out = begin_dir;
				}
				else if(dtime_remaining < entry_plus_curve_time_remaining) // If we won't reach the end of the curve in the dtime remaining:
				{
					m_dist_along_segment += dtime_remaining * vel;
					m_time_along_segment += dtime_remaining;

					const float curve_frac = (m_dist_along_segment - entry_segment_len) / just_curve_len; // Fraction along curve

					const Vec4f orthog_dir = normalise(::removeComponentInDir(waypoints[cur_waypoint_index].exit_segment_unit_dir, begin_dir)); // TODO: handle parallel or antiparallel begin_dir and exit_dir.

					const float angle = curve_frac * waypoints[cur_waypoint_index].curve_angle;
					const float curve_r     = waypoints[cur_waypoint_index].curve_r;
					
					pos_out = entry_pos + begin_dir * entry_segment_len         +     (begin_dir * curve_r * std::sin(angle)   +    orthog_dir * curve_r * (1 - std::cos(angle)));
					dir_out = begin_dir * cos(angle) + orthog_dir * sin(angle);
				}
				else // Else we will end up on the exit segment (the straight line after the curve)
				{
					m_dist_along_segment += dtime_remaining * vel;
					m_time_along_segment += dtime_remaining;

					const float dist_uncovered = curve_len - m_dist_along_segment;

					pos_out = exit_pos - waypoints[cur_waypoint_index].exit_segment_unit_dir * dist_uncovered;
					dir_out = waypoints[cur_waypoint_index].exit_segment_unit_dir;
				}

				dtime_remaining = 0;
				assert(pos_out.isFinite());
				assert(dir_out.isFinite());
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= curve_time_remaining;

				// Advance to start of next segment
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
					assert(pos_out.isFinite());
					assert(dir_out.isFinite());
					break;
				}
				else // else if we start moving again in dtime:
				{
					dtime_remaining -= stop_time_remaining;
					m_time_along_segment += stop_time_remaining;
				}
			}

			const float segment_len = waypoints[cur_waypoint_index].segment_len;// entry_pos.getDist(exit_pos);
			const float segment_traversal_time = segment_len / vel;
			const float travelling_time_along_segment = m_time_along_segment - waypoints[cur_waypoint_index].pause_time; // time spent travelling past the station so far

			float segment_time_remaining = segment_traversal_time - travelling_time_along_segment;
			if(dtime_remaining < segment_time_remaining) // If we won't reach the end of the segment in the dtime remaining:
			{
				m_dist_along_segment += dtime_remaining * vel;
				m_time_along_segment += dtime_remaining;
				dtime_remaining = 0;

				const Vec4f end_dir = normalise(exit_pos - entry_pos);

				const float segment_dist_frac = m_dist_along_segment / segment_len;
				pos_out = Maths::uncheckedLerp(entry_pos, exit_pos, segment_dist_frac);
				// See https://graphtoy.com/?f1(x,t)=smoothstep(0,1,x+0.5)*2-1&v1=true&f2(x,t)=&v2=false&f3(x,t)=&v3=false&f4(x,t)=&v4=false&f5(x,t)=&v5=false&f6(x,t)=&v6=false&grid=1&coords=0,0,2.158305478910566
				dir_out = Maths::uncheckedLerp(begin_dir, end_dir, Maths::smoothStep(0.f, 1.f, segment_dist_frac + 0.5f)*2.f - 1.f); // normalise(exit_pos - entry_pos);
				assert(pos_out.isFinite());
				assert(dir_out.isFinite());
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= segment_time_remaining;

				// Advance to start of next segment
				m_dist_along_segment = 0;
				m_time_along_segment = 0;
				cur_waypoint_index = Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size());
			}
		}
		else // else if waypoint_type == PathWaypointIn::CurveOut:
		{
			assert(waypoints[cur_waypoint_index].waypoint_type == PathWaypointIn::CurveOut);

			const float segment_len = waypoints[cur_waypoint_index].segment_len;
			const float segment_traversal_time = segment_len / vel;
			float segment_time_remaining = segment_traversal_time - m_time_along_segment;
			if(dtime_remaining < segment_time_remaining) // If we won't reach the end of the segment in the dtime remaining:
			{
				m_dist_along_segment += dtime_remaining * vel;
				m_time_along_segment += dtime_remaining;
				dtime_remaining = 0;

				pos_out = Maths::uncheckedLerp(entry_pos, exit_pos, m_dist_along_segment / segment_len);
				dir_out = normalise(exit_pos - entry_pos);
				assert(pos_out.isFinite());
				assert(dir_out.isFinite());
				break;
			}
			else // else if we travel past this segment:
			{
				dtime_remaining -= segment_time_remaining;

				// Advance to start of next segment
				m_dist_along_segment = 0;
				m_time_along_segment = 0;
				cur_waypoint_index = Maths::intMod(cur_waypoint_index + 1, (int)waypoints.size());
			}
		}
	}
}


Vec4f ObjectPathController::evalSegmentCurvePos(int waypoint_index, float frac) const
{
	assert(waypoints[waypoint_index].waypoint_type == PathWaypointIn::CurveIn);

	const Vec4f entry_pos = waypointPos(waypoint_index);
	const Vec4f exit_pos  = waypointPos(waypoint_index + 1);

	const Vec4f begin_dir = normalise(entry_pos - waypointPos(waypoint_index - 1));

	const float curve_len = waypoints[waypoint_index].segment_len;
	const float vel       = waypoints[waypoint_index].speed;

	const float curve_traversal_time = curve_len / vel;

	float t = frac * curve_traversal_time;

	const float entry_segment_len = waypoints[waypoint_index].entry_segment_len;
	const float just_curve_len    = waypoints[waypoint_index].just_curve_len;
	const float entry_segment_time_remaining    = entry_segment_len / vel;
	const float entry_plus_curve_time_remaining = (entry_segment_len + just_curve_len) / vel;
	if(t < entry_segment_time_remaining) // If we won't reach the end of the entry segment in the dtime remaining:
	{
		const float dist_along_segment = t * vel;
		return entry_pos + begin_dir * dist_along_segment;
	}
	else if(t < entry_plus_curve_time_remaining) // If we won't reach the end of the curve in the dtime remaining:
	{
		const float dist_along_segment = t * vel;

		float curve_frac = (dist_along_segment - entry_segment_len) / just_curve_len;

		const Vec4f orthog_dir = normalise(::removeComponentInDir(waypoints[waypoint_index].exit_segment_unit_dir, begin_dir)); // TODO: handle parallel or antiparallel begin_dir and exit_dir.
		const float angle = curve_frac * waypoints[waypoint_index].curve_angle;
		const float curve_r     = waypoints[waypoint_index].curve_r;
		return entry_pos + begin_dir * entry_segment_len         +     (begin_dir * curve_r * std::sin(angle)   +    orthog_dir * curve_r * (1 - std::cos(angle)));
	}
	else // Else we will end up on the exit segment (the straight line after the curve)
	{
		const float dist_along_segment = t * vel;
		const float dist_uncovered = curve_len - dist_along_segment;
		return exit_pos - waypoints[waypoint_index].exit_segment_unit_dir * dist_uncovered;
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

	if(follow_ob_uid.valid())
	{
		// If we are following another object:

		auto res = world_state.objects.find(follow_ob_uid);
		if(res != world_state.objects.end())
		{
			WorldObjectRef lead_ob = res.getValue();

			// Get leading ob waypoint index and dir_along_segment.
			// Walk back along the path by the following distance.

			const int leader_waypoint_index       = lead_ob->waypoint_index;
			const float leader_dist_along_segment = lead_ob->dist_along_segment;

			evalAlongPathDistBackwards(leader_waypoint_index, leader_dist_along_segment, follow_dist, /*pos_out=*/new_pos, /*dir_out=*/new_dir);
		}
		else
		{
			// If the object we are following does not exist, just position at waypoint 0.
			new_pos = waypointPos(0);
			new_dir = Vec4f(1,0,0,0);
		}
	}
	else
	{
		walkAlongPathForTime(dtime, new_pos, new_dir);

		assert(new_pos.isFinite());
		assert(new_dir.isFinite());

		controlled_ob->waypoint_index = cur_waypoint_index;
		controlled_ob->dist_along_segment = m_dist_along_segment;
	}

	const Quatf initial_ob_rot = Quatf::fromAxisAndAngle(normalise(controlled_ob->axis), controlled_ob->angle);

	Quatf final_rot_quat;
	if(orient_along_path)
	{
		const float track_angle = std::atan2(new_dir[1], new_dir[0]);

		const Quatf track_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), track_angle);

		final_rot_quat = track_rot * initial_ob_rot;
	}
	else
		final_rot_quat = initial_ob_rot;

	
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
