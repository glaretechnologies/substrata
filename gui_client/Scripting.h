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
class ObjectPathController;


namespace Scripting
{

void parseXMLScript(WorldObjectRef ob, const std::string& script, double global_time, Reference<ObjectPathController>& path_controller_out);


void evaluateObjectScripts(std::set<WorldObjectRef>& obs_with_scripts, double global_time, double dt, WorldState* world_state, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world,
	int& num_scripts_processed_out);

}
