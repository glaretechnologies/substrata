/*=====================================================================
BiomeManager.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include "../shared/WorldObject.h"
#include "../simpleraytracer/raymesh.h"
#include <map>
#include <string>
class QSettings;
class WorldState;
class PhysicsWorld;
class PhysicsShape;
class MeshManager;
class OpenGLEngine;
class VertexBufferAllocator;
struct GLObject;
class OpenGLTexture;
class OpenGLMeshRenderData;
struct PhysicsShapeData;


/*=====================================================================
BiomeManager
------------

=====================================================================*/
class BiomeManager
{
public:
	BiomeManager();

	// Remove all biome opengl and physics objects from the opengl and physics engines.
	void clear(OpenGLEngine& opengl_engine, PhysicsWorld& physics_world);

	void initTexturesAndModels(const std::string& base_dir_path, OpenGLEngine& opengl_engine, ResourceManager& resource_manager);

	Reference<OpenGLTexture> elm_imposters_tex;
	Reference<OpenGLTexture> elm_bark_tex;
	Reference<OpenGLTexture> elm_leaf_tex;
	Reference<OpenGLTexture> elm_leaf_backface_tex;
	Reference<OpenGLTexture> elm_leaf_transmission_tex;
	//Reference<OpenGLTexture> elm_leaf_normal_map;
	Reference<OpenGLTexture> grass_tex;

	Reference<OpenGLMeshRenderData> elm_tree_mesh_render_data;
	PhysicsShape elm_tree_physics_shape;
	
	std::vector<OpenGLMaterial> elm_tree_gl_materials;
};
