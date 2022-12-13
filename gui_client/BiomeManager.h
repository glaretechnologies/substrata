/*=====================================================================
BiomeManager.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once



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


struct BiomeObInstance
{
	Matrix4f to_world; // Tree to-world
	Matrix4f to_world_no_rot; // Tree to-world without rotation

	Reference<PhysicsObject> physics_object;
};


struct ObBiomeData : public RefCounted
{
	std::vector<Reference<GLObject> > opengl_obs;
	std::vector<Reference<PhysicsObject>> physics_objects;
};


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

	void addObjectToBiome(WorldObject& world_ob, WorldState& world_state, PhysicsWorld& physics_world, MeshManager& mesh_manager, glare::TaskManager& task_manager, OpenGLEngine& opengl_engine,
		ResourceManager& resource_manager);

	bool isObjectInBiome(WorldObject* world_ob) const { return ob_to_biome_data.count(world_ob) > 0; }

	Reference<OpenGLTexture> elm_imposters_tex;
	Reference<OpenGLTexture> elm_leaf_tex;
	Reference<OpenGLTexture> elm_leaf_backface_tex;
	Reference<OpenGLTexture> elm_leaf_transmission_tex;
	Reference<OpenGLTexture> grass_tex;

	void update(const Vec4f& campos, const Vec4f& cam_forwards_ws, const Vec4f& cam_right_ws, const Vec4f& sundir, OpenGLEngine& opengl_engine);

	

private:
	Reference<GLObject> makeElmTreeOb(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator, MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager, PhysicsShape& physics_shape_out);
	Reference<GLObject> makeElmTreeImposterOb(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator, MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager/*, OpenGLTextureRef elm_imposters_tex*/);

	struct Patch;
	void updatePatchSet(std::map<Vec2i, Patch>& patches, float patch_w, const Vec4f& campos, const Vec4f& cam_forwards_ws, const Vec4f& cam_right_ws, const Vec4f& sundir, OpenGLEngine& opengl_engine);

	Reference<GLObject> grass_ob;

	struct Patch
	{
		std::vector<Reference<GLObject> > opengl_obs;
		bool in_new_set;
	};
	std::map<Vec2i, Patch> patches_a;
	std::map<Vec2i, Patch> patches_b;
	std::map<Vec2i, Patch> patches_c;

	std::vector<Reference<PhysicsObject>> park_biome_physics_objects; // objects which had park biome

	js::Vector<PhysicsObject*, 8> physics_obs; // scratch buffer

	js::Vector<Matrix4f, 16> instance_matrices_temp; // scratch buffer

	//std::vector<GLObjectRef> opengl_obs; // Tree obs (not from patches) that have been added to the opengl engine.

	std::map<WorldObject*, Reference<ObBiomeData> > ob_to_biome_data;


	Reference<MeshData> elm_tree_mesh_data;
	Reference<MeshData> elm_tree_imposter_mesh_data;
};
