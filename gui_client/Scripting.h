/*=====================================================================
Scripting.h
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include <utils/LinearIterSet.h>
class WorldState;
class OpenGLEngine;
class PhysicsWorld;
namespace glare { class AudioEngine; }
namespace glare { class TaskManager; }
namespace glare { class TaskGroup; }
class ObjectPathController;


namespace Scripting
{

struct SeatSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	SeatSettings();

	Vec4f seat_position; // in model/object space
	float upper_body_rot_angle; // radians

	float upper_leg_rot_angle; // radians
	float upper_leg_rot_around_thigh_bone_angle;
	float upper_leg_apart_angle;

	float lower_leg_rot_angle; // radians
	float lower_leg_apart_angle; // radians
	float rotate_foot_out_angle; // radians

	float arm_down_angle; // radians
	float arm_out_angle; // radians
};


struct VehicleScriptedSettings : public RefCounted
{
	GLARE_ALIGNED_16_NEW_DELETE

	virtual ~VehicleScriptedSettings() {}

	Quatf model_to_y_forwards_rot_1;
	Quatf model_to_y_forwards_rot_2;

	std::vector<SeatSettings> seat_settings; // Will have size >= 1.
};


class VehicleScript : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual ~VehicleScript() {}

	virtual bool isRightable() const = 0;

	Matrix4f getZUpToModelSpaceTransform() const
	{
		return ((settings->model_to_y_forwards_rot_2 * settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();
	}

	Reference<VehicleScriptedSettings> settings;
};


struct HoverCarScriptSettings : public VehicleScriptedSettings
{
};


class HoverCarScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual bool isRightable() const override { return false; }
};


struct BoatScriptSettings : public VehicleScriptedSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	float thrust_force;
	Vec4f propellor_point_os;
	float propellor_sideways_offset;
	float rudder_deflection_force_factor;
	
	float front_cross_sectional_area;
	float side_cross_sectional_area;
	float top_cross_sectional_area;
};


class BoatScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual bool isRightable() const override { return true; }
};



struct BikeScriptSettings : public VehicleScriptedSettings
{
};

class BikeScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual bool isRightable() const override { return true; }
};



struct CarScriptSettings : public VehicleScriptedSettings
{
	float front_wheel_radius;
	float rear_wheel_radius;
	float front_wheel_width;
	float rear_wheel_width;

	float front_suspension_min_length;
	float front_suspension_max_length;
	float front_wheel_attachment_point_raise_dist;
	float front_suspension_spring_freq;
	float front_suspension_spring_damping;

	float rear_suspension_min_length;
	float rear_suspension_max_length;
	float rear_wheel_attachment_point_raise_dist;
	float rear_suspension_spring_freq;
	float rear_suspension_spring_damping;

	float max_steering_angle;
	float engine_max_torque;
	float engine_max_RPM;
	float max_brake_torque;
	float max_handbrake_torque;

	float longitudinal_friction_factor;
	float lateral_friction_factor;

	std::vector<Vec3f> convex_hull_points;

	std::string front_left_wheel_joint_name;
	std::string front_right_wheel_joint_name;
	std::string back_left_wheel_joint_name;
	std::string back_right_wheel_joint_name;

	std::string front_left_wheel_brake_joint_name;
	std::string front_right_wheel_brake_joint_name;
	std::string back_left_wheel_brake_joint_name;
	std::string back_right_wheel_brake_joint_name;
};


class CarScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual bool isRightable() const override { return true; }
};


struct WinterScriptEvalOutput
{
	Matrix4f ob_to_world;
	Matrix4f ob_to_world_normal_matrix;
	js::AABBox aabb_ws;
	Vec4f translation;
	float det;
};


struct EvalWinterScriptTaskContext
{
	glare::LinearIterSet<WorldObjectRef, WorldObjectRefHash>* obs_with_scripts;
	float use_global_time;
	double dt;
	PhysicsWorld* physics_world;
	glare::AudioEngine* audio_engine;

	js::Vector<WinterScriptEvalOutput>* output;
};


class ObjectScriptsEvaluator : public ThreadSafeRefCounted
{
public:
	void evaluateObjectScripts(glare::LinearIterSet<WorldObjectRef, WorldObjectRefHash>& obs_with_scripts, double global_time, double dt, WorldState* world_state, 
		OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, glare::AudioEngine* audio_engine, glare::TaskManager* task_manager,
		int& num_scripts_processed_out);

	js::Vector<WinterScriptEvalOutput> output;

	Reference<glare::TaskGroup> task_group;

	EvalWinterScriptTaskContext context;
};


void parseXMLScript(WorldObjectRef ob, const std::string& script, double global_time, Reference<ObjectPathController>& path_controller_out, Reference<VehicleScript>& vehicle_script_out);


} // end namespace Scripting
