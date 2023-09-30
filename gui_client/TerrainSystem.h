/*=====================================================================
TerrainSystem.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "IncludeOpenGL.h"
#include "FrameBuffer.h"
#include "OpenGLTexture.h"
#include "OpenGLEngine.h"
#include "PhysicsObject.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Array2D.h"
#include "../maths/Matrix4f.h"
#include "../maths/vec3.h"
#include <string>
#include <map>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;


/*=====================================================================
TerrainSystem
-------------
=====================================================================*/

struct TerrainChunkData
{
	int vert_res_with_borders;
	
	Reference<OpenGLMeshRenderData> mesh_data;

	PhysicsShape physics_shape;
};


class MakeTerrainChunkTask : public glare::Task
{
public:
	virtual void run(size_t thread_index);

	uint64 node_id;
	float chunk_x, chunk_y; // world-space coords of lower left corner of chunk.
	float chunk_w; // Width of chunk in world-space (m)
	bool build_physics_ob;

	TerrainSystem* terrain;

	TerrainChunkData chunk_data; // Result of building chunk

	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
};


class TerrainChunkGeneratedMsg : public ThreadMessage
{
public:
	float chunk_x, chunk_y; // world-space coords of lower left corner of chunk.
	float chunk_w; // Width of chunk in world-space (m)
	uint64 node_id;

	TerrainChunkData chunk_data;
};


// Quad-tree node
struct TerrainNode : public RefCounted
{
	TerrainNode() : building(false), subtree_built(false) {}
	
	GLObjectRef gl_ob;
	PhysicsObjectRef physics_ob;

	GLObjectRef vis_aabb_gl_ob;

	// Objects that have been built, but not inserted into the world yet, because a parent node is waiting for all descendant nodes to finish building.
	GLObjectRef pending_gl_ob;
	PhysicsObjectRef pending_physics_ob;

	TerrainNode* parent;

	js::AABBox aabb; // world space AABB
	int depth;
	uint64 id;
	bool building;
	bool subtree_built; // is subtree built?
	Reference<TerrainNode> children[4];

	std::vector<GLObjectRef> old_subtree_gl_obs; // Objects that are still inserted into opengl engine
	std::vector<PhysicsObjectRef> old_subtree_phys_obs;
};


class TerrainSystem : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainSystem();
	~TerrainSystem();

	void init(OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, const Vec3d& campos, glare::TaskManager* task_manager, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue);

	void shutdown();

	void handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg);

	void updateCampos(const Vec3d& campos);

	std::string getDiagnostics() const;

	float evalTerrainHeight(float p_x, float p_y, float quad_w) const;

	void makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, TerrainChunkData& chunk_data_out) const;
private:
	void updateSubtree(TerrainNode* node, const Vec3d& campos);
	void removeSubtree(TerrainNode* node, std::vector<GLObjectRef>& old_children_gl_obs_in_out, std::vector<PhysicsObjectRef>& old_children_phys_obs_in_out);
	void removeLeafGeometry(TerrainNode* node);
	void createInteriorNodeSubtree(TerrainNode* node, const Vec3d& campos);
	void createSubtree(TerrainNode* node, const Vec3d& campos);
	void insertPendingMeshesForSubtree(TerrainNode* node);
	bool areAllParentSubtreesBuilt(TerrainNode* node);
	void removeAllNodeDataForSubtree(TerrainNode* node);

	GLARE_DISABLE_COPY(TerrainSystem);

	std::map<uint64, TerrainNode*> id_to_node_map;
	uint64 next_id;

	OpenGLMaterial terrain_mat;

	OpenGLMaterial water_mat;

	Reference<TerrainNode> root_node;

	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	glare::TaskManager* task_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;

	Map2DRef heightmap;
	Map2DRef maskmap;

	Map2DRef detail_heightmaps[4];


	IndexBufAllocationHandle vert_res_10_index_buffer;
	IndexBufAllocationHandle vert_res_130_index_buffer;
};
