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


struct TerrainChunk
{
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainChunk() /*: per_ob_vert_data_index(-1)*/ {}

	//std::vector<Reference<OpenGLMeshRenderData> > mesh_data_LODs;

	//std::vector<Reference<OpenGLMeshRenderData> > water_mesh_data_LODs;

	//int per_ob_vert_data_index;

	Vec4f pos;
	float quad_w;
	int lod_level;

	GLObjectRef gl_ob;

	PhysicsObjectRef physics_ob;

	//GLObjectRef water_gl_ob;

	//Array2D<float> terrain_heightfield;
	//Array2D<float> water_heightfield;

	//bool needed;
};


struct TerrainChunkData
{
	//js::Vector<uint8, 16> vert_data;
	//js::Vector<uint8, 16> index_data;
	Reference<OpenGLMeshRenderData> mesh_data;

	PhysicsShape physics_shape;
};

class MakeTerrainChunkTask : public glare::Task
{
public:
	virtual void run(size_t thread_index);

	//int chunk_id;
	uint64 node_id;
	//int chunk_x_i;
	//int chunk_y_i;
	float chunk_x, chunk_y, chunk_w; // Chunk x and y coords, and width (all in metres)
	//int lod_level;
	//int depth;
	bool build_physics_ob;

	TerrainSystem* terrain;

	TerrainChunkData chunk_data;

	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
};


class TerrainChunkGeneratedMsg : public ThreadMessage
{
public:
	//int chunk_x_i;
	//int chunk_y_i;
	float chunk_x, chunk_y, chunk_w;
	//int lod_level;
	uint64 node_id;

	TerrainChunkData chunk_data;
};


struct CoverType
{

};


// Quad-tree node
struct TerrainNode : public RefCounted
{
	TerrainNode() : building(false), subtree_built(false)/*, num_children_built(0)*/ {}
	
	GLObjectRef gl_ob;
	PhysicsObjectRef physics_ob;

	GLObjectRef vis_aabb_gl_ob;

	// Objects that have been built, but not inserted into the world yet, because a parent node is waiting for all descendant nodes to finish building.
	GLObjectRef pending_gl_ob;
	PhysicsObjectRef pending_physics_ob;

	TerrainNode* parent;

	js::AABBox aabb;
	//int lod_level;
	int depth;
	uint64 id;
	bool building;
	bool subtree_built; // is subtree built?
	//int num_children_built;
	Reference<TerrainNode> children[4];

	std::vector<GLObjectRef> old_subtree_gl_obs; // Objects that are still inserted into opengl engine
	std::vector<PhysicsObjectRef> old_subtree_phys_obs;
};


#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif

class TerrainSystem : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainSystem();
	~TerrainSystem();

	// Adds initial terrain generation tasks to task_manager
	void init(OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, const Vec3d& campos, glare::TaskManager* task_manager, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue);

	void shutdown();

	void handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg);

	void updateCampos(const Vec3d& campos);

	std::string getDiagnostics() const;

	//void makeTerrainChunk(OpenGLEngine& engine, int x, int y, int lod_level);

	float evalTerrainHeight(float p_x, float p_y, float quad_w, bool water) const;

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
public:
	void makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, bool water, TerrainChunkData& chunk_data_out) const;

	//std::map<Vec2<int>, TerrainChunk> chunks;

	//std::set<Vec2<int>> chunks_building;

	std::map<uint64, TerrainNode*> id_to_node_map;
	uint64 next_id;

	OpenGLMaterial terrain_mat;

	OpenGLMaterial water_mat;

	int water_fbm_tex_location;
	int water_cirrus_tex_location;
	int water_sundir_cs_location;

	Reference<TerrainNode> root_node;

	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	glare::TaskManager* task_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;

	Map2DRef heightmap;
	Map2DRef small_dune_heightmap;
};

#ifdef _WIN32
#pragma warning(pop)
#endif
