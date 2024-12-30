/*=====================================================================
Scripting.cpp
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "Scripting.h"


#include "WinterShaderEvaluator.h"
#include "PhysicsWorld.h"
#include "WorldState.h"
#include "ObjectPathController.h"
#include "../audio/AudioEngine.h"
#include "../shared/WorldObject.h"
#include <opengl/OpenGLEngine.h>
#include <utils/Timer.h>
#include <utils/Lock.h>
#include <utils/IndigoXMLDoc.h>
#include <utils/Parser.h>
#include <utils/XMLParseUtils.h>
#include "superluminal/PerformanceAPI.h"


namespace Scripting
{


const Vec3d parseVec3Directly(pugi::xml_node elem)
{
	const char* const child_text = elem.child_value();

	Parser parser(child_text, std::strlen(child_text));

	parser.parseWhiteSpace();

	Vec3d v;
	if(!parser.parseDouble(v.x))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(child_text) + "'." + XMLParseUtils::elemContext(elem));
	parser.parseWhiteSpace();
	if(!parser.parseDouble(v.y))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(child_text) + "'." + XMLParseUtils::elemContext(elem));
	parser.parseWhiteSpace();
	if(!parser.parseDouble(v.z))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(child_text) + "'." + XMLParseUtils::elemContext(elem));
	parser.parseWhiteSpace();

	// We should be at the end of the string now
	if(parser.notEOF())
		throw glare::Exception("Parse error while parsing Vec3 from '" + std::string(child_text) + "'." + XMLParseUtils::elemContext(elem));

	return v;
}


const Vec3d parseVec3(pugi::xml_node elem, const char* elemname)
{
	pugi::xml_node childnode = elem.child(elemname);
	if(!childnode)
		throw glare::Exception(std::string("could not find element '") + elemname + "'." + XMLParseUtils::elemContext(elem));

	return parseVec3Directly(childnode);
}


const Vec3d parseVec3WithDefault(pugi::xml_node elem, const char* elemname, const Vec3d& defualt)
{
	pugi::xml_node childnode = elem.child(elemname);
	if(childnode)
		return parseVec3Directly(childnode);
	else
		return defualt;
}


Quatf parseRotationWithDefault(pugi::xml_node elem, const char* elemname, const Quatf& default_rot)
{
	pugi::xml_node childnode = elem.child(elemname);
	if(!childnode)
		return default_rot;

	const Vec3d v = parseVec3(elem, elemname);

	if(v.length() < 1.0e-4f)
		return Quatf::identity();
	else
		return Quatf::fromAxisAndAngle(normalise(v.toVec4fVector()), (float)v.length());
}


SeatSettings::SeatSettings()
{
	seat_position = Vec4f(0,0,0,1);
	upper_body_rot_angle = 0.4;

	upper_leg_rot_angle = 1.3;
	upper_leg_rot_around_thigh_bone_angle = 0;
	upper_leg_apart_angle = 0;

	lower_leg_rot_angle = -0.5;
	lower_leg_apart_angle = 0.0;
	rotate_foot_out_angle = 0.0;

	arm_down_angle = 2.1;
	arm_out_angle = 0.3;
}


static SeatSettings parseSeatSettings(pugi::xml_node seat_elem, const SeatSettings& default_seat_settings)
{
	SeatSettings seat_settings;
	seat_settings.seat_position			= parseVec3(seat_elem, "seat_position").toVec4fPoint();
	seat_settings.upper_body_rot_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "upper_body_rot_angle", default_seat_settings.upper_body_rot_angle);
	seat_settings.upper_leg_rot_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "upper_leg_rot_angle", default_seat_settings.upper_leg_rot_angle);
	seat_settings.upper_leg_rot_around_thigh_bone_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "upper_leg_rot_around_thigh_bone_angle", default_seat_settings.upper_leg_rot_around_thigh_bone_angle);
	seat_settings.upper_leg_apart_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "upper_leg_apart_angle", default_seat_settings.upper_leg_apart_angle);
	seat_settings.lower_leg_rot_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "lower_leg_rot_angle", default_seat_settings.lower_leg_rot_angle);
	seat_settings.lower_leg_apart_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "lower_leg_apart_angle", default_seat_settings.lower_leg_apart_angle);
	seat_settings.rotate_foot_out_angle	= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "rotate_foot_out_angle", default_seat_settings.rotate_foot_out_angle);
	seat_settings.arm_down_angle		= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "arm_down_angle", default_seat_settings.arm_down_angle);
	seat_settings.arm_out_angle			= (float)XMLParseUtils::parseDoubleWithDefault(seat_elem, "arm_out_angle", default_seat_settings.arm_out_angle);

	return seat_settings;
}


void parseXMLScript(WorldObjectRef ob, const std::string& script, double global_time, Reference<ObjectPathController>& path_controller_out, Reference<VehicleScript>& vehicle_script_out)
{
	try
	{
		IndigoXMLDoc doc(script.c_str(), script.size());

		pugi::xml_node root_elem = doc.getRootElement(); // Expected to be 'script'.  TODO: check?

		pugi::xml_node follow_path_elem = root_elem.child("follow_path");
		if(follow_path_elem)
		{
			std::vector<PathWaypointIn> waypoints;

			const double time_offset = XMLParseUtils::parseDoubleWithDefault(follow_path_elem, "time_offset", 0.0);
			const double default_speed = XMLParseUtils::parseDoubleWithDefault(follow_path_elem, "speed", 2.0);

			UID follow_ob_uid = UID::invalidUID();
			if(follow_path_elem.child("follow_ob_uid"))
				follow_ob_uid = UID(XMLParseUtils::parseInt(follow_path_elem, "follow_ob_uid"));

			const double follow_dist = XMLParseUtils::parseDoubleWithDefault(follow_path_elem, "follow_dist", 0.0);

			for(pugi::xml_node waypoint_elem = follow_path_elem.child("waypoint"); waypoint_elem; waypoint_elem = waypoint_elem.next_sibling("waypoint"))
			{
				const Vec3d pos = parseVec3(waypoint_elem, "pos");

				const std::string type = ::stripHeadAndTailWhitespace(XMLParseUtils::parseString(waypoint_elem, "type"));

				const double pause_time = XMLParseUtils::parseDoubleWithDefault(waypoint_elem, "pause_time", 10.0);

				const double speed = XMLParseUtils::parseDoubleWithDefault(waypoint_elem, "speed", default_speed);

				PathWaypointIn waypoint;
				waypoint.pos = pos.toVec4fPoint();
				if(type == "CurveIn")
					waypoint.waypoint_type = PathWaypointIn::CurveIn;
				else if(type == "CurveOut")
					waypoint.waypoint_type = PathWaypointIn::CurveOut;
				else if(type == "Stop")
					waypoint.waypoint_type = PathWaypointIn::Station;
				else
					throw glare::Exception("Invalid waypoint type: '" + type + "'.");

				waypoint.pause_time = (float)pause_time;
				waypoint.speed = (float)speed;

				waypoints.push_back(waypoint);
			}

			if(ob.nonNull())
				path_controller_out = new ObjectPathController(ob, waypoints, global_time + time_offset, /*follow ob UID=*/follow_ob_uid, /*follow dist=*/(float)follow_dist);
		}

		// ----------- hover car -----------
		{
			pugi::xml_node hover_car_elem = root_elem.child("hover_car");
			if(hover_car_elem)
			{
				Reference<HoverCarScript> hover_car_script = new HoverCarScript();

				hover_car_script->settings = new HoverCarScriptSettings();

				hover_car_script->settings->model_to_y_forwards_rot_1 = parseRotationWithDefault(hover_car_elem, "model_to_y_forwards_rot_1", Quatf::identity());
				hover_car_script->settings->model_to_y_forwards_rot_2 = parseRotationWithDefault(hover_car_elem, "model_to_y_forwards_rot_2", Quatf::identity());

				for(pugi::xml_node seat_elem = hover_car_elem.child("seat"); seat_elem; seat_elem = seat_elem.next_sibling("seat"))
				{
					SeatSettings default_seat_settings;
					SeatSettings seat_settings = parseSeatSettings(seat_elem, default_seat_settings);

					hover_car_script->settings->seat_settings.push_back(seat_settings);
				}
			
				if(hover_car_script->settings->seat_settings.empty())
					throw glare::Exception("hover_car element must have at least one seat element");

				vehicle_script_out = hover_car_script;
			}
		}

		// ----------- boat -----------
		{
			pugi::xml_node boat_elem = root_elem.child("boat");
			if(boat_elem)
			{
				Reference<BoatScript> boat_script = new BoatScript();

				Reference<BoatScriptSettings> boat_settings = new BoatScriptSettings();
				boat_script->settings = boat_settings;

				boat_settings->thrust_force = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "thrust_force", 40000.0);
				boat_settings->propellor_point_os = parseVec3WithDefault(boat_elem, "propellor_point_os", Vec3d(0, 0.2, -6.2)).toVec4fPoint(); // +z is backwards in beatminer boat model
				boat_settings->propellor_sideways_offset = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "propellor_sideways_offset", 1.0);
				boat_settings->rudder_deflection_force_factor = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "rudder_deflection_force_factor", 500.0);
				
				boat_settings->front_cross_sectional_area = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "front_cross_sectional_area", 2.0);
				boat_settings->side_cross_sectional_area = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "side_cross_sectional_area", 4.0);
				boat_settings->top_cross_sectional_area = (float)XMLParseUtils::parseDoubleWithDefault(boat_elem, "top_cross_sectional_area", 8.0);

				boat_settings->model_to_y_forwards_rot_1 = parseRotationWithDefault(boat_elem, "model_to_y_forwards_rot_1", Quatf::identity());
				boat_settings->model_to_y_forwards_rot_2 = parseRotationWithDefault(boat_elem, "model_to_y_forwards_rot_2", Quatf::identity());

				for(pugi::xml_node seat_elem = boat_elem.child("seat"); seat_elem; seat_elem = seat_elem.next_sibling("seat"))
				{
					SeatSettings default_seat_settings;
					SeatSettings seat_settings = parseSeatSettings(seat_elem, default_seat_settings);

					boat_settings->seat_settings.push_back(seat_settings);
				}
			
				if(boat_settings->seat_settings.empty())
					throw glare::Exception("boat element must have at least one seat element");

				vehicle_script_out = boat_script;
			}
		}

		// ----------- bike -----------
		{
			pugi::xml_node bike_elem = root_elem.child("bike");
			if(bike_elem)
			{
				Reference<BikeScript> bike_script = new BikeScript();

				bike_script->settings = new BikeScriptSettings();

				bike_script->settings->model_to_y_forwards_rot_1 = parseRotationWithDefault(bike_elem, "model_to_y_forwards_rot_1", Quatf::identity());
				bike_script->settings->model_to_y_forwards_rot_2 = parseRotationWithDefault(bike_elem, "model_to_y_forwards_rot_2", Quatf::identity());

				for(pugi::xml_node seat_elem = bike_elem.child("seat"); seat_elem; seat_elem = seat_elem.next_sibling("seat"))
				{
					SeatSettings default_seat_settings;
					default_seat_settings.upper_leg_rot_around_thigh_bone_angle = 0.27;
					default_seat_settings.upper_leg_apart_angle = 0.17;
					default_seat_settings.lower_leg_apart_angle = 0.1;
					default_seat_settings.rotate_foot_out_angle = 0.5;
					default_seat_settings.arm_down_angle = 2.7;
					default_seat_settings.arm_out_angle = 0.2;
					SeatSettings seat_settings = parseSeatSettings(seat_elem, default_seat_settings);

					bike_script->settings->seat_settings.push_back(seat_settings);
				}

				if(bike_script->settings->seat_settings.empty())
					throw glare::Exception("bike element must have at least one seat element");

				vehicle_script_out = bike_script;
			}
		}
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


void evalObjectScript(WorldObject* ob, float use_global_time, double dt, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, glare::AudioEngine* audio_engine, Matrix4f& ob_to_world_out)
{
	CybWinterEnv winter_env;
	winter_env.instance_index = 0;
	winter_env.num_instances = 1;

#if !defined(EMSCRIPTEN)
	if(ob->script_evaluator->jitted_evalRotation)
	{
		const Vec4f rot = ob->script_evaluator->evalRotation(use_global_time, winter_env);
		ob->angle = rot.length();
		if(isFinite(ob->angle))
		{
			if(ob->angle > 0)
				ob->axis = Vec3f(normalise(rot));
			else
				ob->axis = Vec3f(1, 0, 0);
		}
		else
		{
			ob->angle = 0;
			ob->axis = Vec3f(1, 0, 0);
		}
	}

	if(ob->script_evaluator->jitted_evalTranslation)
	{
		ob->translation = ob->script_evaluator->evalTranslation(use_global_time, winter_env);
	}
#endif

	// Compute object-to-world matrix, similarly to obToWorldMatrix().  Do it here so we can reuse some components of the computation.
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);
	const Vec4f translation = pos + ob->translation;

	if(!translation.isFinite()) // Avoid hitting assert in Jolt (and other potential problems) if translation is Nan, or Inf.
		return;

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec4f use_scale = ob->scale.toVec4fVector();
	if(use_scale[0] == 0) use_scale[0] = 1.0e-6f;
	if(use_scale[1] == 0) use_scale[1] = 1.0e-6f;
	if(use_scale[2] == 0) use_scale[2] = 1.0e-6f;

	const Vec4f unit_axis = normalise(ob->axis.toVec4fVector());
	const Matrix4f R = Matrix4f::rotationMatrix(unit_axis, ob->angle);
	Matrix4f ob_to_world;
	ob_to_world.setColumn(0, R.getColumn(0) * use_scale[0]);
	ob_to_world.setColumn(1, R.getColumn(1) * use_scale[1]);
	ob_to_world.setColumn(2, R.getColumn(2) * use_scale[2]);
	ob_to_world.setColumn(3, translation);

	/* Compute upper-left inverse transpose matrix.
	upper left inverse transpose:
	= ((RS)^-1)^T
	= (S^1 R^1)^T
	= R^1^T S^1^T
	= R S^1

	Compute upper-left adjugate transpose matrix.
	upper left adjugate transpose:
	= adj(RS)^T = det(RS) ((RS)^-1)^T
	= det(RS) (S^1 R^1)^T
	= det(RS) R^1^T S^1^T
	= det(RS) R S^1
	*/

	const float det = use_scale[0] * use_scale[1] * use_scale[2];

	assert(Maths::approxEq<float>(det, (R * Matrix4f::scaleMatrix(use_scale[0], use_scale[1], use_scale[2])).upperLeftDeterminant()));

	const Vec4f det_over_scale = maskWToZero(div(Vec4f(det), use_scale)); // det(RS) S^-1

	// Right-multiplying with a scale matrix is equivalent to multiplying column 0 with scale_x, column 1 with scale_y etc.
	Matrix4f ob_to_world_normal_matrix;
	ob_to_world_normal_matrix.setColumn(0, R.getColumn(0) * copyToAll<0>(det_over_scale));
	ob_to_world_normal_matrix.setColumn(1, R.getColumn(1) * copyToAll<1>(det_over_scale));
	ob_to_world_normal_matrix.setColumn(2, R.getColumn(2) * copyToAll<2>(det_over_scale));
	ob_to_world_normal_matrix.setColumn(3, Vec4f(0, 0, 0, 1));

	// Update transform in 3d engine.
	GLObject* gl_ob = ob->opengl_engine_ob.ptr();
	if(gl_ob)
	{
		gl_ob->ob_to_world_matrix = ob_to_world;
		gl_ob->ob_to_world_normal_matrix = ob_to_world_normal_matrix;
		gl_ob->ob_to_world_matrix_determinant = det;
		gl_ob->aabb_ws = gl_ob->mesh_data->aabb_os.transformedAABBFast(ob_to_world);
		opengl_engine->objectTransformDataChanged(*gl_ob);

		// Update object world space AABB (used for computing LOD level).
		// For objects with animated rotation, we want to compute an AABB without rotation, otherwise we can get a world-space AABB
		// that effectively oscillates in size.  See https://youtu.be/Wo_PauArb6A for an example.
		// This is bad because it can cause the object to oscillate between LOD levels.
		// The AABB will be somewhat wrong, but hopefully it shouldn't matter too much.
#if !defined(EMSCRIPTEN)
		if(ob->script_evaluator->jitted_evalRotation)
		{
			ob->doTransformChangedIgnoreRotation(translation, use_scale);
		}
		else
#endif
		{
			ob->doTransformChanged(ob_to_world, use_scale);
		}


		// TODO: need to call assignLightsToObject() somehow
		// opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
	}

	// Update in physics engine
	if(ob->physics_object.nonNull())
		physics_world->moveKinematicObject(*ob->physics_object, translation, Quatf::fromAxisAndAngle(unit_axis, ob->angle), (float)dt);


	if(ob->opengl_light.nonNull())
	{
		ob->opengl_light->gpu_data.dir = normalise(ob_to_world * Vec4f(0, 0, -1, 0));

		opengl_engine->setLightPos(ob->opengl_light, setWToOne(translation));
	}

	// Update audio source for the object, if it has one.
	if(ob->audio_source.nonNull())
	{
		ob->audio_source->pos = ob->getCentroidWS();
		audio_engine->sourcePositionUpdated(*ob->audio_source);
	}

	ob_to_world_out = ob_to_world;
}


void evalObjectInstanceScript(InstanceInfo* ob, float use_global_time, double dt, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, Matrix4f& ob_to_world_out)
{
	CybWinterEnv winter_env;
	winter_env.instance_index = ob->instance_index;
	winter_env.num_instances = ob->num_instances;

#if !defined(EMSCRIPTEN)
	if(ob->script_evaluator->jitted_evalRotation)
	{
		const Vec4f rot = ob->script_evaluator->evalRotation(use_global_time, winter_env);
		ob->angle = rot.length();
		if(isFinite(ob->angle))
		{
			if(ob->angle > 0)
				ob->axis = Vec3f(normalise(rot));
			else
				ob->axis = Vec3f(1, 0, 0);
		}
		else
		{
			ob->angle = 0;
			ob->axis = Vec3f(1, 0, 0);
		}
	}

	if(ob->script_evaluator->jitted_evalTranslation)
	{
		if(ob->prototype_object)
			ob->pos = ob->prototype_object->pos;
		ob->translation = ob->script_evaluator->evalTranslation(use_global_time, winter_env);
	}
#endif

	// Compute object-to-world matrix, similarly to obToWorldMatrix().  Do it here so we can reuse some components of the computation.
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);
	const Vec4f translation = pos + ob->translation;

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec4f use_scale = ob->scale.toVec4fVector();
	if(use_scale[0] == 0) use_scale[0] = 1.0e-6f;
	if(use_scale[1] == 0) use_scale[1] = 1.0e-6f;
	if(use_scale[2] == 0) use_scale[2] = 1.0e-6f;

	const Vec4f unit_axis = normalise(ob->axis.toVec4fVector());
	const Matrix4f rot = Matrix4f::rotationMatrix(unit_axis, ob->angle);
	Matrix4f ob_to_world;
	ob_to_world.setColumn(0, rot.getColumn(0) * use_scale[0]);
	ob_to_world.setColumn(1, rot.getColumn(1) * use_scale[1]);
	ob_to_world.setColumn(2, rot.getColumn(2) * use_scale[2]);
	ob_to_world.setColumn(3, translation);

	// Update in physics engine
	if(ob->physics_object.nonNull())
		physics_world->moveKinematicObject(*ob->physics_object, translation, Quatf::fromAxisAndAngle(unit_axis, ob->angle), (float)dt);

	ob_to_world_out = ob_to_world;
}


void evaluateObjectScripts(std::set<WorldObjectRef>& obs_with_scripts, double global_time, double dt, WorldState* world_state, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, glare::AudioEngine* audio_engine,
	int& num_scripts_processed_out)
{
	// Evaluate scripts on objects
	if(world_state)
	{
		PERFORMANCEAPI_INSTRUMENT("eval scripts");
		int num_scripts_processed = 0;

		Lock lock(world_state->mutex);

		// When float values get too large, the gap between successive values gets greater than the frame period,
		// resulting in 'jumpy' transformations.  So mod the double value down to a smaller range (that wraps e.g. once per hour)
		// and then cast to float.
		const float use_global_time = (float)Maths::doubleMod(global_time, 3600);

		for(auto it = obs_with_scripts.begin(); it != obs_with_scripts.end(); ++it)
		{
			WorldObject* ob = it->getPointer();

			assert(ob->script_evaluator.nonNull());
			if(ob->script_evaluator.nonNull())
			{
				Matrix4f ob_to_world;
				evalObjectScript(ob, use_global_time, dt, opengl_engine, physics_world, audio_engine, ob_to_world);
				num_scripts_processed++;

				// If this object has instances (and has a graphics ob):
				if(!ob->instances.empty() && ob->opengl_engine_ob.nonNull())
				{
					// Update instance ob-to-world transform based on the script.
					// Compute AABB over all instances of the object
					js::AABBox all_instances_aabb_ws = js::AABBox::emptyAABBox();
					const js::AABBox aabb_os = ob->opengl_engine_ob->mesh_data->aabb_os;
					for(size_t z=0; z<ob->instances.size(); ++z)
					{
						InstanceInfo* instance = &ob->instances[z];
						Matrix4f instance_ob_to_world;
						evalObjectInstanceScript(instance, use_global_time, dt, opengl_engine, physics_world, instance_ob_to_world); // Updates instance->physics_object->ob_to_world
						num_scripts_processed++;

						all_instances_aabb_ws.enlargeToHoldAABBox(aabb_os.transformedAABBFast(instance_ob_to_world));

						ob->instance_matrices[z] = instance_ob_to_world;
					}

					// Manually set AABB of instanced object.
					// NOTE: we will avoid opengl_engine->updateObjectTransformData(), since it doesn't handle instances currently.
					ob->opengl_engine_ob->aabb_ws = all_instances_aabb_ws;

					// Also update instance_matrix_vbo.
					assert(ob->instance_matrices.size() == ob->instances.size());
					if(ob->opengl_engine_ob->instance_matrix_vbo.nonNull())
					{
						ob->opengl_engine_ob->instance_matrix_vbo->updateData(ob->instance_matrices.data(), ob->instance_matrices.dataSizeBytes());
					}
				}
			}
		}

		num_scripts_processed_out = num_scripts_processed;
	}
	else
	{
		num_scripts_processed_out = 0;
	}
}


} // end namespace Scripting
