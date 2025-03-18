/*=====================================================================
Scripting.h
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
class WorldState;
class OpenGLEngine;
class PhysicsWorld;
namespace glare { class AudioEngine; }
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
};

class CarScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual bool isRightable() const override { return true; }
};





void parseXMLScript(WorldObjectRef ob, const std::string& script, double global_time, Reference<ObjectPathController>& path_controller_out, Reference<VehicleScript>& vehicle_script_out);


void evaluateObjectScripts(std::set<WorldObjectRef>& obs_with_scripts, double global_time, double dt, WorldState* world_state, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, glare::AudioEngine* audio_engine,
	int& num_scripts_processed_out);

} // end namespace Scripting
