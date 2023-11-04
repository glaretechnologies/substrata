/*=====================================================================
TerrainSystem.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "TerrainScattering.h"
#include "PhysicsObject.h"
#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/OpenGLEngine.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <utils/Array2D.h>
#include <utils/BumpAllocator.h>
#include <maths/Matrix4f.h>
#include <maths/vec3.h>
#include <string>
#include <map>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;
class BiomeManager;


/*=====================================================================
TerrainSystem
-------------
=====================================================================*/


//-------------------------- Specification of Terrain - heightmaps and mask maps to use on each section --------------------------
// Similar to TerrainSpec in WorldSettings.h but with paths instead of URLs

struct TerrainPathSpecSection
{
	int x, y; // section coordinates.  (0,0) is section centered on world origin.

	std::string heightmap_path;
	std::string mask_map_path;
};

struct TerrainPathSpec
{
	std::vector<TerrainPathSpecSection> section_specs;

	std::string detail_col_map_paths[4];
	std::string detail_height_map_paths[4];

	float terrain_section_width_m;
	float default_terrain_z;
	float water_z;
	uint32 flags;
};
//-------------------------- End Specification of Terrain --------------------------


struct TerrainDataSection
{
	std::string heightmap_path;
	std::string mask_map_path;

	Map2DRef heightmap;
	OpenGLTextureRef heightmap_gl_tex;
	Map2DRef maskmap;
	OpenGLTextureRef mask_gl_tex;
};


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

	friend class TerrainTests;
	friend class TerrainScattering;
	friend class MakeTerrainChunkTask;

	void init(const TerrainPathSpec& spec, const std::string& base_dir_path, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, BiomeManager* biome_manager, const Vec3d& campos, glare::TaskManager* task_manager, glare::BumpAllocator& bump_allocator, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue);

	void shutdown();

	// A texture that will be used by the terrain system has been loaded into OpenGL.
	void handleTextureLoaded(const std::string& path, const Map2DRef& map);

	void handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg);

	void updateCampos(const Vec3d& campos, glare::BumpAllocator& bump_allocator);

	void rebuildScattering();

	void invalidateVegetationMap(const js::AABBox& aabb_ws);

	std::string getDiagnostics() const;


	Colour4f evalTerrainMask(float p_x, float p_y) const;
	float evalTerrainHeight(float p_x, float p_y, float quad_w) const;

private:
	void makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, TerrainChunkData& chunk_data_out) const;
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
	BiomeManager* biome_manager;
	glare::TaskManager* task_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;

	TerrainScattering terrain_scattering;

public:
	static const int TERRAIN_DATA_SECTION_RES = 8;
	static const int TERRAIN_SECTION_OFFSET = TERRAIN_DATA_SECTION_RES / 2;
private:
	TerrainDataSection terrain_data_sections[TERRAIN_DATA_SECTION_RES*TERRAIN_DATA_SECTION_RES];
	
	Map2DRef detail_heightmaps[4];

	IndexBufAllocationHandle vert_res_10_index_buffer;
	IndexBufAllocationHandle vert_res_130_index_buffer;

	TerrainPathSpec spec;

	// Scale factor for world-space -> heightmap UV conversion.
	// Its reciprocal is the width of the terrain in metres.
	float terrain_section_w;
	float terrain_scale_factor;

	std::vector<GLObjectRef> water_gl_obs;
};
