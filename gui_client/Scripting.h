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

	Vec4f seat_position; // in model/object space
	float upper_body_rot_angle; // radians
	float upper_leg_rot_angle; // radians
	float lower_leg_rot_angle; // radians
};


struct VehicleScriptedSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Quatf model_to_y_forwards_rot_1;
	Quatf model_to_y_forwards_rot_2;

	std::vector<SeatSettings> seat_settings; // Will have size >= 1.
};


class VehicleScript : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	virtual ~VehicleScript() {}

	VehicleScriptedSettings settings;
};


class HoverCarScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE
};


class BikeScript : public VehicleScript
{
public:
	GLARE_ALIGNED_16_NEW_DELETE
};



void parseXMLScript(WorldObjectRef ob, const std::string& script, double global_time, Reference<ObjectPathController>& path_controller_out, Reference<VehicleScript>& vehicle_script_out);


void evaluateObjectScripts(std::set<WorldObjectRef>& obs_with_scripts, double global_time, double dt, WorldState* world_state, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, glare::AudioEngine* audio_engine,
	int& num_scripts_processed_out);

}
