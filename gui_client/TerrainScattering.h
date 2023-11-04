/*=====================================================================
TerrainScattering.h
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "OpenGLEngine.h"
#include "PhysicsObject.h"
#include <graphics/NonZeroMipMap.h>
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Array2D.h"
#include "../utils/BumpAllocator.h"
#include "../maths/Matrix4f.h"
#include "../maths/vec3.h"
#include "../maths/vec2.h"
class OpenGLEngine;
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;
class BiomeManager;
class TerrainSystem;


/*=====================================================================
TerrainScattering
-----------------
Code for scattering trees, grass and other vegetation over a terrain.
=====================================================================*/

class TerrainScattering : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainScattering();
	~TerrainScattering();

	void init(const std::string& base_dir_path, TerrainSystem* terrain_system, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, BiomeManager* biome_manager, const Vec3d& campos, glare::BumpAllocator& bump_allocator);

	void shutdown();

	void rebuild();

	void invalidateVegetationMap(const js::AABBox& aabb_ws);

	void updateCampos(const Vec3d& campos, glare::BumpAllocator& bump_allocator);

	
	// Needs to be same as PrecomputedPoint in build_imposters_compute_shader.glsl
	struct PrecomputedPoint
	{
		Vec2f uv;
		float scale_factor;
		float rot;
	};

	struct VegetationLocationInfo
	{
		Vec4f pos;
		float scale;
	};

	struct LargeTreeChunk
	{
		js::Vector<VegetationLocationInfo, 16> locations;
		GLObjectRef imposters_gl_ob;
	};


	struct SmallTreeObjectChunk
	{
		js::Vector<GLObjectRef, 16> gl_obs;
		js::Vector<PhysicsObjectRef, 16> physics_obs;
	};


	struct GrassChunk
	{
		GLObjectRef grass_gl_ob;
	};

	struct NearGrassChunk
	{
		GLObjectRef grass_gl_ob;
	};


	struct GridScatterChunk
	{
		GLObjectRef imposters_gl_ob;
	};
	struct GridScatter : public RefCounted
	{
		GridScatter()
		:	last_centre(-1000000, -1000000), use_wind_vert_shader(false)
		{}

		Array2D<GridScatterChunk> chunks;
		Vec2i last_centre;
		float chunk_width;
		int grid_res;

		bool use_wind_vert_shader;

		float begin_fade_in_distance;
		float end_fade_in_distance;
		float begin_fade_out_distance;
		float end_fade_out_distance;
		OpenGLTextureRef imposter_texture;
		OpenGLTextureRef imposter_normal_map;

		float density;
		float base_scale;
		float imposter_width_over_height;

		js::Vector<PrecomputedPoint, 16> precomputed_points;

		Reference<SSBO> precomputed_points_ssbo;
	};



private:
	void updateCamposForGridScatter(const Vec3d& campos, glare::BumpAllocator& bump_allocator, GridScatter& grid_scatter);
	void makeTreeChunk(int chunk_x_index, int chunk_y_index, glare::BumpAllocator& bump_allocator, LargeTreeChunk& chunk);
	//void makeGrassChunk(int chunk_x_index, int chunk_y_index, glare::BumpAllocator& bump_allocator, GrassChunk& chunk);
	//void makeNearGrassChunk(int chunk_x_index, int chunk_y_index, glare::BumpAllocator& bump_allocator, NearGrassChunk& chunk);
	void makeGridScatterChunk(int chunk_x_index, int chunk_y_index, glare::BumpAllocator& bump_allocator, GridScatter& grid_scatter, GridScatterChunk& chunk);
	void updateGridScatterChunkWithComputeShader(int chunk_x_index, int chunk_y_index, GridScatter& grid_scatter, GridScatterChunk& chunk);

	void buildPrecomputedPoints(float chunk_w_m, float density, glare::BumpAllocator& bump_allocator, js::Vector<PrecomputedPoint, 16>& precomputed_points);
	GLObjectRef buildVegLocationsAndImposterGLOb(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, float imposter_width_over_height, glare::BumpAllocator& bump_allocator, js::Vector<PrecomputedPoint, 16>* precomputed_points, js::Vector<VegetationLocationInfo, 16>& locations_out);
	GLObjectRef makeUninitialisedImposterGLOb(glare::BumpAllocator& bump_allocator, const js::Vector<PrecomputedPoint, 16>& precomputed_points);

	void buildVegLocationInfo(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, glare::BumpAllocator& bump_allocator, js::Vector<VegetationLocationInfo, 16>& locations_out);
	//void buildVegLocationInfoWithPrecomputedPoints(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, glare::BumpAllocator& bump_allocator, js::Vector<PrecomputedPoint, 16>& points, js::Vector<VegetationLocationInfo, 16>& locations_out);
	void rebuildDetailMaskMapSection(int section_x, int section_y);

	GLARE_DISABLE_COPY(TerrainScattering);

	TerrainSystem* terrain_system;
	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	BiomeManager* biome_manager;
	
	// Grid of LargeTreeChunk: Each LargeTreeChunk contains a list of tree positions in that chunk, plus an imposter GLObject.
	Array2D<LargeTreeChunk> large_tree_chunks;
	int last_centre_x, last_centre_y;
	
	// Grid of SmallTreeObjectChunk: Each SmallTreeObjectChunk contains a list of individual tree GLObjects and physics objects.
	Array2D<SmallTreeObjectChunk> tree_ob_chunks;
	Vec2i last_ob_centre_i;

	//Array2D<GrassChunk> grass_chunks;
	//Vec2i last_grass_centre;

	//Array2D<NearGrassChunk> near_grass_chunks;
	//Vec2i last_near_grass_centre;


	std::vector<Reference<GridScatter>> grid_scatters;

	//js::Vector<VegetationLocationInfo, 16> temp_locations;

	Reference<OpenGLMeshRenderData> grass_clump_meshdata;

	Reference<OpenGLTexture> grass_texture;
	Reference<OpenGLTexture> grass_normal_map;


	Reference<OpenGLProgram> build_imposters_prog;
	int terrain_height_map_location;
	int terrain_mask_tex_location;
	int terrain_fbm_tex_location;
	int terrain_detail_mask_tex_location;

	Reference<SSBO> chunk_info_ssbo;

	GLuint timer_query_ids[2];

	struct DetailMaskMapSection
	{
		Reference<OpenGLTexture> mask_map_gl_tex;
		ImageMapUInt8Ref detail_mask_map;
		bool gl_tex_valid;

		NonZeroMipMap non_zero_mip_map;
	};

	static const int DETAIL_MASK_MAP_SECTION_RES = 8;
	DetailMaskMapSection detail_mask_map_sections[DETAIL_MASK_MAP_SECTION_RES*DETAIL_MASK_MAP_SECTION_RES];


	OpenGLTextureRef default_detail_mask_tex;

};
