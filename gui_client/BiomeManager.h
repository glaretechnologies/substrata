/*=====================================================================
BiomeManager.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLEngine.h>
#include "../shared/WorldObject.h"
#include "../simpleraytracer/raymesh.h"
#include <map>
#include <string>
class QSettings;
class WorldState;
class PhysicsWorld;
class MeshManager;
class OpenGLEngine;


struct BiomeObInstance
{
	Matrix4f to_world; // Tree to-world
	Matrix4f to_world_no_rot; // Tree to-world without rotation
};


/*=====================================================================
BiomeManager
------------

=====================================================================*/
class BiomeManager
{
public:
	BiomeManager();

	void addObjectToBiome(WorldObject& world_ob, WorldState& world_state, PhysicsWorld& physics_world, MeshManager& mesh_manager, glare::TaskManager& task_manager, OpenGLEngine& opengl_engine,
		ResourceManager& resource_manager);

	OpenGLTextureRef elm_imposters_tex;

private:
	
};
