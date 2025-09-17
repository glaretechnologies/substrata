/*=====================================================================
TerrainScattering.cpp
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainScattering.h"


#include "TerrainSystem.h"
#include "OpenGLEngine.h"
#include "OpenGLShader.h"
#include "BiomeManager.h"
#include "TerrainTests.h"
#include "graphics/PerlinNoise.h"
#include "PhysicsWorld.h"
#include "../shared/ImageDecoding.h"
#include <utils/TaskManager.h>
#include <utils/FileUtils.h>
#include <utils/ContainerUtils.h>
#include <utils/RuntimeCheck.h>
#include "graphics/Voronoi.h"
#include "graphics/FormatDecoderGLTF.h"
#include "graphics/PNGDecoder.h"
#include "graphics/EXRDecoder.h"
#include "graphics/jpegdecoder.h"
#include "graphics/SRGBUtils.h"
#include "opengl/GLMeshBuilding.h"
#include "opengl/MeshPrimitiveBuilding.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "../dll/include/IndigoMesh.h"
#include <tracy/Tracy.hpp>


#if !defined(OSX) && !defined(EMSCRIPTEN)
#define USE_COMPUTE_SHADER 1
#endif


// Just for Mac
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER						0x90D2
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER								0x91B9
#endif


static const float detail_mask_map_width_m = 256.f; // NOTE: needs to match DETAIL_MASK_MAP_WIDTH_M define in shaders\build_imposters_compute_shader.glsl
static const size_t detail_mask_map_width_px = 512;

static const float LARGE_TREE_CHUNK_W = 256; // metres
static const int LARGE_TREE_CHUNK_GRID_RES = 17;

static const float TREE_OB_CHUNK_W = 16; // metres
static const int TREE_OB_CHUNK_GRID_RES = 17;

static const int LOG_2_CHUNK_W_RATIO = 4; // = log_2(LARGE_TREE_CHUNK_W / TREE_OB_CHUNK_W)
//static_assert((1 << LOG_2_CHUNK_W_RATIO) == (int)(LARGE_TREE_CHUNK_W / TREE_OB_CHUNK_W), "LOG_2_CHUNK_W_RATIO");

//static const float GRASS_CHUNK_W = 4; // metres
//static const int GRASS_CHUNK_GRID_RES = 17;

//static const float NEAR_GRASS_CHUNK_W = 1; // metres
//static const int NEAR_GRASS_CHUNK_GRID_RES = 9;


// NOTE: These can't clash with pther SSBO binding points used.
static const int VERTEX_DATA_BINDING_POINT_INDEX = 10;
static const int PRECOMPUTED_POINTS_BINDING_POINT_INDEX = 11;
static const int CHUNK_INFO_BINDING_POINT_INDEX = 12;


TerrainScattering::TerrainScattering()
{
	large_tree_chunks.resize(LARGE_TREE_CHUNK_GRID_RES, LARGE_TREE_CHUNK_GRID_RES);
	last_centre_x = -1000000;
	last_centre_y = -1000000;
	any_large_tree_chunk_invalidated = false;
	num_imposter_obs_inserted = 0;

	tree_ob_chunks.resize(TREE_OB_CHUNK_GRID_RES, TREE_OB_CHUNK_GRID_RES);
	last_ob_centre_i = Vec2i(-1000000);

	for(int y=0; y<TREE_OB_CHUNK_GRID_RES; ++y)
	for(int x=0; x<TREE_OB_CHUNK_GRID_RES; ++x)
	{
		SmallTreeObjectChunk& chunk = tree_ob_chunks.elem(x, y);
		chunk.gl_obs.reserve(4);
		chunk.physics_obs.reserve(4);
	}

	//grass_chunks.resize(GRASS_CHUNK_GRID_RES, GRASS_CHUNK_GRID_RES);
	//last_grass_centre = Vec2i(-1000000);

	//near_grass_chunks.resize(NEAR_GRASS_CHUNK_GRID_RES, NEAR_GRASS_CHUNK_GRID_RES);
	//last_near_grass_centre = Vec2i(-1000000);
}


TerrainScattering::~TerrainScattering()
{
}


// Scale factor for world-space -> heightmap UV conversion.
// Its reciprocal is the width of the terrain in metres.
static const float terrain_section_w = 8 * 1024;
static const float terrain_scale_factor = 1.f / terrain_section_w;


// Needs to match ChunkInfo in build_imposters_compute_shader.glsl
struct ShaderChunkInfo
{
	int chunk_x_index;
	int chunk_y_index;
	float chunk_w_m;
	int section_x; // unoffset
	int section_y; // unoffset
	float base_scale;
	float imposter_width_over_height;
	float terrain_scale_factor;
	uint32 vert_data_offset_B;
};


void TerrainScattering::init(const std::string& base_dir_path, TerrainSystem* terrain_system_, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_, BiomeManager* biome_manager_, const Vec3d& campos, 
	glare::StackAllocator& bump_allocator)
{
	terrain_system = terrain_system_;
	opengl_engine = opengl_engine_;
	physics_world = physics_world_;
	biome_manager = biome_manager_;

	any_large_tree_chunk_invalidated = false;

#if !EMSCRIPTEN // Grass scattering compute shader doesn't work in emscripten so don't bother loading grass textures for now.
	{
		const std::string grass_billboard_tex_path = base_dir_path + "/data/resources/grass clump billboard 2.png";
		opengl_engine->removeOpenGLTexture(OpenGLTextureKey(grass_billboard_tex_path));

		Reference<Map2D> map = ImageDecoding::decodeImage(".", grass_billboard_tex_path);
		//map.downcast<ImageMapUInt8>()->floodFillFromOpaquePixels(/*src_alpha_threshold=*/100, /*update_alpha_threshold=*/100, /*iterations=*/100); // TEMP
		//PNGDecoder::write(*map.downcast<ImageMapUInt8>(), "grass clump billboard 2 floodfilled.png");

		TextureParams params;
		params.wrapping = OpenGLTexture::Wrapping_Clamp;
		params.allow_compression = false;
		//grass_texture = opengl_engine->getTexture(grass_billboard_tex_path, params);
		grass_texture = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey(grass_billboard_tex_path), *map, params);
	}

	{
		const std::string grass_normal_map_path = base_dir_path + "/data/resources/grass clump billboard 2 normals.png";
		opengl_engine->removeOpenGLTexture(OpenGLTextureKey(grass_normal_map_path));

		Reference<Map2D> map = ImageDecoding::decodeImage(".", grass_normal_map_path);
		//map.downcast<ImageMapUInt8>()->floodFillFromOpaquePixels(/*src_alpha_threshold=*/100, /*update_alpha_threshold=*/100, /*iterations=*/100); // TEMP
		//PNGDecoder::write(*map.downcast<ImageMapUInt8>(), "grass clump billboard 2 normals floodfilled.png");

		TextureParams params;
		params.wrapping = OpenGLTexture::Wrapping_Clamp;
		params.allow_compression = false;
		params.use_sRGB = false;
		grass_normal_map = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey(grass_normal_map_path), *map, params);
	}
#endif

	//grass_texture = opengl_engine->getTexture("N:\\substrata\\trunk\\resources\\obstacle.png");

	{
		ImageMapUInt8Ref default_mask_map = new ImageMapUInt8(1, 1, 4);
		default_mask_map->getPixel(0, 0)[0] = 0;
		default_mask_map->getPixel(0, 0)[1] = 0;
		default_mask_map->getPixel(0, 0)[2] = 0;
		default_mask_map->getPixel(0, 0)[3] = 0;
		this->default_detail_mask_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_detail_mask_tex__"), *default_mask_map);
	}

	for(int i=0; i<DETAIL_MASK_MAP_SECTION_RES*DETAIL_MASK_MAP_SECTION_RES; ++i)
	{
		detail_mask_map_sections[i].gl_tex_valid = false;
		detail_mask_map_sections[i].detail_mask_map.setAsNotIndependentlyHeapAllocated();
	}


	//{
	//	GLTFLoadedData gltf_data;
	//	Reference<BatchedMesh> batched_mesh = FormatDecoderGLTF::loadGLBFile("D:\\models\\grass_clump1.glb", gltf_data);
	//	grass_clump_meshdata = GLMeshBuilding::buildBatchedMesh(opengl_engine->vert_buf_allocator.ptr(), batched_mesh, /*skip_opengl_calls=*/true);
	//	//	
	//}


	const float widths[]           = { 1,    2,    4,    8,    24 };
	//const float densities[]        = { 20,   10,   5,    2.5,  0.15f };
	const float densities[]        = { 20,   7,   4,    1.25,  0.15f };
	const float begin_fade_fracs[] = { 0.5f, 0.5f, 0.5f, 0.8f, 0.9f };

	for(size_t i=0; i<staticArrayNumElems(widths); ++i)
	{
		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = widths[i];
		scatter->grid_res = 17;
		scatter->use_wind_vert_shader = true;
		const float max_draw_dist = (scatter->grid_res / 2) * scatter->chunk_width;
		scatter->begin_fade_in_distance = 0;
		scatter->end_fade_in_distance = 0.001f;
		scatter->begin_fade_out_distance = max_draw_dist * begin_fade_fracs[i];
		scatter->end_fade_out_distance   = max_draw_dist;
		scatter->imposter_texture = grass_texture;
		scatter->imposter_normal_map = grass_normal_map;
		scatter->density = densities[i];
		scatter->base_scale = 0.5f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}

	/*for(int i=0; i<4; ++i)
	{
		const float chunk_width = std::pow(2.0f, i); // 1, 2, 4, 8
		const float density = 20 / chunk_width; // (chunk_width*chunk_width); // 100, 25, 6.25, 1.5625

		const int grid_res = 17;
		const float max_draw_dist = (grid_res / 2) * chunk_width;
		const float begin_fade_out_distance = max_draw_dist * 0.6f;
		const float end_fade_out_distance   = max_draw_dist;

		const float prev_chunk_width = std::pow(2.0f, i-1);
		const float prev_max_draw_dist = (grid_res / 2) * prev_chunk_width;
		const float prev_begin_fade_out_distance = prev_max_draw_dist * 0.5f;
		const float prev_end_fade_out_distance   = prev_max_draw_dist;

		const float begin_fade_in_distance = 0; // (i == 0) ? 0.0    : prev_begin_fade_out_distance * 0.6f;
		const float end_fade_in_distance   = 0.001f; // 1(i == 0) ? 0.001f : prev_end_fade_out_distance   * 0.6f;

		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = chunk_width;
		scatter->grid_res = grid_res;
		scatter->begin_fade_in_distance = begin_fade_in_distance;
		scatter->end_fade_in_distance = end_fade_in_distance;
		scatter->begin_fade_out_distance = begin_fade_out_distance;
		scatter->end_fade_out_distance   = end_fade_out_distance;
		scatter->imposter_texture = grass_texture;
		scatter->density = density;
		scatter->base_scale = 0.5f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}

	{
		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = 32;
		scatter->grid_res = 17;
		const float max_draw_dist = (scatter->grid_res / 2) * scatter->chunk_width;
		scatter->begin_fade_in_distance = 0;
		scatter->end_fade_in_distance = 0.001;
		scatter->begin_fade_out_distance = max_draw_dist * 0.9f;
		scatter->end_fade_out_distance   = max_draw_dist;
		scatter->imposter_texture = grass_texture;
		scatter->density = 0.1f;
		scatter->base_scale = 0.5f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}*/

	for(size_t i=0; i<grid_scatters.size(); ++i)
	{
		Reference<GridScatter> scatter = grid_scatters[i];

		buildPrecomputedPoints(scatter->chunk_width, scatter->density, bump_allocator, scatter->precomputed_points);

		// Build precomputed_points SSBO
#if USE_COMPUTE_SHADER
		scatter->precomputed_points_ssbo = new SSBO();
		scatter->precomputed_points_ssbo->allocate(scatter->precomputed_points.dataSizeBytes(), /*map memory=*/false);
		scatter->precomputed_points_ssbo->updateData(0, scatter->precomputed_points.data(), scatter->precomputed_points.dataSizeBytes());
#endif
	}


	/*{
		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = 8;
		scatter->grid_res = 17;
		scatter->begin_fade_out_distance = 0; // (scatter->grid_res / 2       ) * scatter->chunk_width;
		scatter->end_fade_out_distance   = 0.1; // (scatter->grid_res / 2 + 0.5f) * scatter->chunk_width;
		scatter->imposter_texture = grass_texture;
		scatter->density = 2.5;
		scatter->base_scale = 0.1f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}

	{
		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = 16;
		scatter->grid_res = 17;
		scatter->begin_fade_out_distance = 0; // (scatter->grid_res / 2       ) * scatter->chunk_width;
		scatter->end_fade_out_distance   = 0.1; // (scatter->grid_res / 2 + 0.5f) * scatter->chunk_width;
		scatter->imposter_texture = grass_texture;
		scatter->density = 0.7f;
		scatter->base_scale = 0.1f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}
	{
		Reference<GridScatter> scatter = new GridScatter();
		scatter->chunk_width = 32;
		scatter->grid_res = 17;
		scatter->begin_fade_out_distance = 0; // (scatter->grid_res / 2       ) * scatter->chunk_width;
		scatter->end_fade_out_distance   = 0.1; // (scatter->grid_res / 2 + 0.5f) * scatter->chunk_width;
		scatter->imposter_texture = grass_texture;
		scatter->density = 0.2f;
		scatter->base_scale = 0.1f;
		scatter->imposter_width_over_height = 1.6f;
		scatter->chunks.resize(scatter->grid_res, scatter->grid_res);

		grid_scatters.push_back(scatter);
	}*/


	//----------------------- Scatter compute shader -------------------------
	
#if USE_COMPUTE_SHADER
	const bool use_imposter_compute_shader = true;
#else
	const bool use_imposter_compute_shader = false; // Mac doesn't support compute shaders
#endif

	if(use_imposter_compute_shader)
	{
		const std::string use_shader_dir = opengl_engine->getDataDir() + "/shaders";
		OpenGLShaderRef build_imposters_compute_shader = new OpenGLShader(use_shader_dir + "/build_imposters_compute_shader.glsl", opengl_engine->getVersionDirective(), 
			opengl_engine->getPreprocessorDefinesWithCommonVertStructs(), GL_COMPUTE_SHADER);
		build_imposters_prog = new OpenGLProgram("build imposters", build_imposters_compute_shader, /*frag shader=*/NULL, opengl_engine->getAndIncrNextProgramIndex(), /*wait for build to complete=*/true);

		terrain_height_map_location       = build_imposters_prog->getUniformLocation("terrain_height_map");
		terrain_mask_tex_location         = build_imposters_prog->getUniformLocation("terrain_mask_tex");
		terrain_fbm_tex_location          = build_imposters_prog->getUniformLocation("fbm_tex");
		terrain_detail_mask_tex_location  = build_imposters_prog->getUniformLocation("detail_mask_tex");

		// Build chunk_info_ssbo
		chunk_info_ssbo = new SSBO();
		chunk_info_ssbo->allocate(sizeof(ShaderChunkInfo), false);
	
		// Bind stuff that doesn't change per chunk
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, /*binding point=*/CHUNK_INFO_BINDING_POINT_INDEX, chunk_info_ssbo->handle);

		bindShaderStorageBlockToProgram(build_imposters_prog, "VertexData", VERTEX_DATA_BINDING_POINT_INDEX);
		bindShaderStorageBlockToProgram(build_imposters_prog, "PrecomputedPoints", PRECOMPUTED_POINTS_BINDING_POINT_INDEX);
		bindShaderStorageBlockToProgram(build_imposters_prog, "ChunkInfoData", CHUNK_INFO_BINDING_POINT_INDEX);
	}

	glGenQueries(2, timer_query_ids);
}


void TerrainScattering::shutdown()
{
	// Remove any opengl and physics objects stored in our grids
	//-------------- Remove from large_tree_chunks ---------------
	for(int j=0; j<LARGE_TREE_CHUNK_GRID_RES; ++j)
	for(int i=0; i<LARGE_TREE_CHUNK_GRID_RES; ++i)
	{
		LargeTreeChunk& chunk = large_tree_chunks.elem(i, j);
		if(chunk.imposters_gl_ob.nonNull())
			opengl_engine->removeObject(chunk.imposters_gl_ob);
	}
	num_imposter_obs_inserted = 0;

	//-------------- Remove from tree_ob_chunks ---------------
	for(int y=0; y<TREE_OB_CHUNK_GRID_RES; ++y)
	for(int x=0; x<TREE_OB_CHUNK_GRID_RES; ++x)
	{
		SmallTreeObjectChunk& chunk = tree_ob_chunks.elem(x, y);

		for(size_t z=0; z<chunk.gl_obs.size(); ++z)
			opengl_engine->removeObject(chunk.gl_obs[z]);
		chunk.gl_obs.clear();

		for(size_t z=0; z<chunk.physics_obs.size(); ++z)
			physics_world->removeObject(chunk.physics_obs[z]);
		chunk.physics_obs.clear();
	}

	//-------------- Remove from grid_scatters ---------------
	for(size_t i=0; i<grid_scatters.size(); ++i)
	{
		Reference<GridScatter> scatter = grid_scatters[i];
		for(size_t y=0; y<scatter->chunks.getHeight(); ++y)
		for(size_t x=0; x<scatter->chunks.getWidth() ; ++x)
		{
			GridScatterChunk& chunk = scatter->chunks.elem(x, y);
			if(chunk.imposters_gl_ob.nonNull())
				opengl_engine->removeObject(chunk.imposters_gl_ob);
		}
	}
}


void TerrainScattering::rebuild()
{
	last_centre_x = -100000;
	last_centre_y = -100000;

	last_ob_centre_i = Vec2i(-100000);

	for(size_t i=0; i<grid_scatters.size(); ++i)
		grid_scatters[i]->last_centre = Vec2i(-100000);
}



void TerrainScattering::invalidateVegetationMap(const js::AABBox& aabb_ws)
{
	// conPrint("TerrainScattering::invalidateVegetationMap()");

	// Invalidate large tree chunks that intersect the AABB
	{
		any_large_tree_chunk_invalidated = true;

		const int start_x = Maths::floorToInt(aabb_ws.min_[0] / LARGE_TREE_CHUNK_W);
		const int start_y = Maths::floorToInt(aabb_ws.min_[1] / LARGE_TREE_CHUNK_W);
		const int end_x   = Maths::floorToInt(aabb_ws.max_[0] / LARGE_TREE_CHUNK_W) + 1; // exclusive
		const int end_y   = Maths::floorToInt(aabb_ws.max_[1] / LARGE_TREE_CHUNK_W) + 1; // exclusive

		const int x0 = last_centre_x        - LARGE_TREE_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around camera position
		const int y0 = last_centre_y        - LARGE_TREE_CHUNK_GRID_RES/2;
		const int wrapped_x0 = Maths::intMod(x0, LARGE_TREE_CHUNK_GRID_RES);
		const int wrapped_y0 = Maths::intMod(y0, LARGE_TREE_CHUNK_GRID_RES);

		// Iterate over wrapped coordinates
		LargeTreeChunk* const large_tree_chunks_data = large_tree_chunks.getData();
		for(int j=0; j<LARGE_TREE_CHUNK_GRID_RES; ++j)
		for(int i=0; i<LARGE_TREE_CHUNK_GRID_RES; ++i)
		{
			// Compute unwrapped cell indices:
			const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);
			const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);

			if( x >= start_x && x < end_x && // If cell index lies in invalidated region:
				y >= start_y && y < end_y)
			{
				large_tree_chunks_data[i + j * LARGE_TREE_CHUNK_GRID_RES].valid = false; // Mark as invalid
			}
		}
	}

	// TODO: invalidate this stuff in a more precise way like with large tree chunks above.
	last_ob_centre_i = Vec2i(-100000);

	for(size_t i=0; i<grid_scatters.size(); ++i)
		grid_scatters[i]->last_centre = Vec2i(-100000);


	{
		const int start_x = myClamp(Maths::floorToInt(aabb_ws.min_[0] / detail_mask_map_width_m)     + DETAIL_MASK_MAP_SECTION_RES/2, 0, DETAIL_MASK_MAP_SECTION_RES);
		const int start_y = myClamp(Maths::floorToInt(aabb_ws.min_[1] / detail_mask_map_width_m)     + DETAIL_MASK_MAP_SECTION_RES/2, 0, DETAIL_MASK_MAP_SECTION_RES);
		const int end_x   = myClamp(Maths::floorToInt(aabb_ws.max_[0] / detail_mask_map_width_m) + 1 + DETAIL_MASK_MAP_SECTION_RES/2, 0, DETAIL_MASK_MAP_SECTION_RES); // exclusive
		const int end_y   = myClamp(Maths::floorToInt(aabb_ws.max_[1] / detail_mask_map_width_m) + 1 + DETAIL_MASK_MAP_SECTION_RES/2, 0, DETAIL_MASK_MAP_SECTION_RES);

		for(int y=start_y; y<end_y; ++y)
		for(int x=start_x; x<end_x; ++x)
		{
			// conPrint("invalidating section " + toString(x) + ", " + toString(y));

			detail_mask_map_sections[x + y*DETAIL_MASK_MAP_SECTION_RES].gl_tex_valid = false;
		}
	}
}


void TerrainScattering::rebuildDetailMaskMapSection(int section_x, int section_y)
{
	// conPrint("TerrainScattering::rebuildDetailMaskMapSection: section_x: " + toString(section_x) + ", section_y: " + toString(section_y));
	ZoneScopedN("TerrainScattering::rebuildDetailMaskMapSection"); // Tracy profiler

	DetailMaskMapSection& section = detail_mask_map_sections[section_x + section_y*DETAIL_MASK_MAP_SECTION_RES];
	if(section.mask_map_gl_tex.isNull())
	{
		section.mask_map_gl_tex = new OpenGLTexture(detail_mask_map_width_px, detail_mask_map_width_px, /*opengl_engine=*/opengl_engine,
			ArrayRef<uint8>(NULL, 0),
			OpenGLTextureFormat::Format_Greyscale_Uint8,
			OpenGLTexture::Filtering_Bilinear
		);
	}


	const Vec2f botleft_ws(
		(section_x - DETAIL_MASK_MAP_SECTION_RES/2) * detail_mask_map_width_m,
		(section_y - DETAIL_MASK_MAP_SECTION_RES/2) * detail_mask_map_width_m
	);


#if 1

	// Do pass to render detail vegetation mask
	opengl_engine->renderMaskMap(*section.mask_map_gl_tex, botleft_ws, /*world capture width=*/detail_mask_map_width_m);
	section.gl_tex_valid = true;

	// Read texture back to main memory
	Timer timer;
	if(temp_detail_mask_map.isNull())
		temp_detail_mask_map = new ImageMapUInt8(detail_mask_map_width_px, detail_mask_map_width_px, 1);
#if defined(EMSCRIPTEN)
	// glGetTexImage isn't supported in WebGL, so use glReadPixels (which reads from the frame buffer) instead.

	opengl_engine->mask_map_frame_buffer->bindForReading();
	glReadPixels(0, 0, detail_mask_map_width_px, detail_mask_map_width_px, GL_RED, GL_UNSIGNED_BYTE, temp_detail_mask_map->getData());
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// For the web, since we can't use compute shaders, we don't need the GPU-side mask map, which is just used for the grass scatter compute shader.
	// Delete mask_map_gl_tex, detatch it from mask map framebuffer first.
	opengl_engine->mask_map_frame_buffer->detachTexture(*section.mask_map_gl_tex, GL_COLOR_ATTACHMENT0);
	section.mask_map_gl_tex = nullptr; 
#else
	section.mask_map_gl_tex->readBackTexture(/*mipmap level=*/0, ArrayRef<uint8>(temp_detail_mask_map->getData(), temp_detail_mask_map->getDataSize()));
#endif
	//conPrint("\nTerrain scattering: reading back texture took " + timer.elapsedStringMSWIthNSigFigs(4) + "");

	// Convert temp_detail_mask_map (8-bit) to section.detail_mask_map (1-bit)
	{
		//Timer timer2;
		section.detail_mask_map.resizeNoCopy(detail_mask_map_width_px, detail_mask_map_width_px);

		const uint8* const src_data = temp_detail_mask_map->getData();
		static_assert(detail_mask_map_width_px % 32 == 0);
		for(size_t i=0; i<detail_mask_map_width_px*detail_mask_map_width_px/32; ++i)
		{
			uint32 v = 0;
			for(int z=0; z<32; ++z)
				v |= ((uint32)src_data[i * 32 + z] >> 7) << z; // Extract most-significant bit of the uint8 value by shifting right by 7, then OR it into the correct place in v.
			section.detail_mask_map.data.v[i] = v;
		}
		//conPrint("Conversion of temp_detail_mask_map to section.detail_mask_map (1-bit) took " + timer2.elapsedStringMSWIthNSigFigs());
	}


#else
	//if(section.detail_mask_map)
	{
		section.detail_mask_map.resizeNoCopy(detail_mask_map_width_px, detail_mask_map_width_px);
		section.detail_mask_map.zero();
		//section.detail_mask_map = new ImageMapUInt1(detail_mask_map_width_px, detail_mask_map_width_px, 3); // GL_RGB
		//section.detail_mask_map->zero();
	}
	section.gl_tex_valid = true;
#endif


	// Build non-zero mipmap
	//timer.reset();
	section.non_zero_mip_map.build(section.detail_mask_map, /*channel=*/0);
	//conPrint("non_zero_mip_map.build " + timer.elapsedStringMSWIthNSigFigs(4) + "\n");

	//PNGDecoder::write(*section.detail_mask_map, "detail_mask_map_" + toString(section_x) + "_" + toString(section_y) + ".png");
}


/*
                   large chunk (0, 0)                                     large chunk (1, 0)
==============================================================================================================
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||----------------------------------------------------||----------------------------------------------------||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||----------------------------------------------------||-------------------------X--------------------------||
||           |             |             |            ||           |             |             |            ||
||           |             |             |            ||           |             |             |            ||
||           |             |     X       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||-------------------------------|--------------------||----------------------------------------------------||
||           |             |     |       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||           |             |     |       |            ||           |             |             |            ||
||-------------------------------|--------------------||----------------------------------------------------||
                                 |                                               |
                  (chunk_x_index, chunk_y_index)              (large_chunk_centre_x_i, large_chunk_centre_y_i) = (1, 0)
                = (2, 1)



*/
void TerrainScattering::updateCampos(const Vec3d& campos, glare::StackAllocator& bump_allocator)
{
	// Build detail mask maps for any invalid sections.
	for(int y=0; y<DETAIL_MASK_MAP_SECTION_RES; ++y)
	for(int x=0; x<DETAIL_MASK_MAP_SECTION_RES; ++x)
	{
		if(!detail_mask_map_sections[x + y*DETAIL_MASK_MAP_SECTION_RES].gl_tex_valid)
			rebuildDetailMaskMapSection(x, y);
	}


#if 1
	const int large_chunk_centre_x = Maths::floorToInt(campos.x / LARGE_TREE_CHUNK_W);
	const int large_chunk_centre_y = Maths::floorToInt(campos.y / LARGE_TREE_CHUNK_W);
	
	// Update tree info and imposters
	if(large_chunk_centre_x != last_centre_x || large_chunk_centre_y != last_centre_y || any_large_tree_chunk_invalidated)
	{
		Timer timer;
		[[maybe_unused]] int num_chunks_updated = 0;

		const int x0     = large_chunk_centre_x - LARGE_TREE_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = large_chunk_centre_y - LARGE_TREE_CHUNK_GRID_RES/2;
		const int old_x0 = last_centre_x        - LARGE_TREE_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = last_centre_y        - LARGE_TREE_CHUNK_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     LARGE_TREE_CHUNK_GRID_RES); // x0 mod LARGE_TREE_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     LARGE_TREE_CHUNK_GRID_RES); // y0 mod LARGE_TREE_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, LARGE_TREE_CHUNK_GRID_RES);
		const int old_wrapped_y0 = Maths::intMod(old_y0, LARGE_TREE_CHUNK_GRID_RES);

		// Iterate over wrapped coordinates
		LargeTreeChunk* const large_tree_chunks_data = large_tree_chunks.getData();
		for(int j=0; j<LARGE_TREE_CHUNK_GRID_RES; ++j)
		for(int i=0; i<LARGE_TREE_CHUNK_GRID_RES; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);

			LargeTreeChunk& chunk = large_tree_chunks_data[i + j * LARGE_TREE_CHUNK_GRID_RES];

			if( old_x < x0 || old_x >= x0 + LARGE_TREE_CHUNK_GRID_RES || 
				old_y < y0 || old_y >= y0 + LARGE_TREE_CHUNK_GRID_RES ||
				!chunk.valid	
				)
			{
				// This chunk is out of range of new camera, or has been invalidated

				// Unload objects in this cell, if any:
				if(chunk.imposters_gl_ob.nonNull())
				{
					opengl_engine->removeObject(chunk.imposters_gl_ob);
					num_imposter_obs_inserted--;
					chunk.locations.clear();
					chunk.imposters_gl_ob = NULL;
				}

				// Load new objects:
			
				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : LARGE_TREE_CHUNK_GRID_RES);
				assert(x >= x0 && x < x0 + LARGE_TREE_CHUNK_GRID_RES);
				assert(y >= y0 && y < y0 + LARGE_TREE_CHUNK_GRID_RES);

				makeTreeChunk(x, y, bump_allocator, chunk); // Sets chunk.tree_info and chunk.imposters_gl_ob.
				if(chunk.imposters_gl_ob.nonNull())
				{
					opengl_engine->addObject(chunk.imposters_gl_ob);
					num_imposter_obs_inserted++;
				}
				chunk.valid = true;

				num_chunks_updated++;
			}
		}

		last_centre_x = large_chunk_centre_x;
		last_centre_y = large_chunk_centre_y;
		any_large_tree_chunk_invalidated = false;

		// conPrint("Updating imposter chunks took " + timer.elapsedString() + " (num_chunks_updated: " + toString(num_chunks_updated) + ")");
		// printVar(num_imposter_obs_inserted);
	}

	// Update individual tree info (small chunks)
	const Vec2i centre_i(
		Maths::floorToInt(campos.x / TREE_OB_CHUNK_W),
		Maths::floorToInt(campos.y / TREE_OB_CHUNK_W));

	if(centre_i != last_ob_centre_i)
	{
		PCG32 rng(1);

		const int x0     = centre_i.x         - TREE_OB_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = centre_i.y         - TREE_OB_CHUNK_GRID_RES/2;
		const int old_x0 = last_ob_centre_i.x - TREE_OB_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = last_ob_centre_i.y - TREE_OB_CHUNK_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     TREE_OB_CHUNK_GRID_RES); // x0 mod TREE_OB_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     TREE_OB_CHUNK_GRID_RES); // y0 mod TREE_OB_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, TREE_OB_CHUNK_GRID_RES);
		const int old_wrapped_y0 = Maths::intMod(old_y0, TREE_OB_CHUNK_GRID_RES);

		// Iterate over wrapped coordinates
		SmallTreeObjectChunk* const tree_ob_chunks_data = tree_ob_chunks.getData();
		for(int j=0; j<TREE_OB_CHUNK_GRID_RES; ++j)
		for(int i=0; i<TREE_OB_CHUNK_GRID_RES; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : TREE_OB_CHUNK_GRID_RES);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : TREE_OB_CHUNK_GRID_RES);

			if( old_x < x0 || old_x >= x0 + TREE_OB_CHUNK_GRID_RES || 
				old_y < y0 || old_y >= y0 + TREE_OB_CHUNK_GRID_RES)
			{
				// This chunk is out of range of new camera.

				SmallTreeObjectChunk& chunk = tree_ob_chunks_data[i + j * TREE_OB_CHUNK_GRID_RES];

				//------------------- Unload objects in this cell, if any: -------------------
				for(size_t z=0; z<chunk.gl_obs.size(); ++z)
					opengl_engine->removeObject(chunk.gl_obs[z]);
				chunk.gl_obs.clear();

				for(size_t z=0; z<chunk.physics_obs.size(); ++z)
					physics_world->removeObject(chunk.physics_obs[z]);
				chunk.physics_obs.clear();


				//------------------- Load new objects: -------------------
				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : TREE_OB_CHUNK_GRID_RES);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : TREE_OB_CHUNK_GRID_RES);
				assert(x >= x0 && x < x0 + TREE_OB_CHUNK_GRID_RES);
				assert(y >= y0 && y < y0 + TREE_OB_CHUNK_GRID_RES);

				const js::AABBox chunk_aabb_ws(
					Vec4f(x       * TREE_OB_CHUNK_W, y       * TREE_OB_CHUNK_W, -1.0e10f, 1),
					Vec4f((x + 1) * TREE_OB_CHUNK_W, (y + 1) * TREE_OB_CHUNK_W,  1.0e10f, 1)
				);

				// Get LargeTreeChunk that this small tree object chunk lies in:
				const int large_tree_chunk_x = x >> LOG_2_CHUNK_W_RATIO;
				const int large_tree_chunk_y = y >> LOG_2_CHUNK_W_RATIO;
				const int large_tree_chunk_wrapped_x = Maths::intMod(large_tree_chunk_x, LARGE_TREE_CHUNK_GRID_RES);
				const int large_tree_chunk_wrapped_y = Maths::intMod(large_tree_chunk_y, LARGE_TREE_CHUNK_GRID_RES);
				const LargeTreeChunk& large_tree_chunk = large_tree_chunks.elem(large_tree_chunk_wrapped_x, large_tree_chunk_wrapped_y);
				const js::Vector<VegetationLocationInfo, 16>& tree_info_ = large_tree_chunk.locations;
				const size_t tree_info_size = tree_info_.size();
				const VegetationLocationInfo* const tree_info = tree_info_.data();
				for(size_t z=0; z<tree_info_size; ++z)
				{
					if(chunk_aabb_ws.contains(tree_info[z].pos))
					{
						//-------------- Create opengl tree object --------------
						GLObjectRef gl_ob = opengl_engine->allocateObject();
						gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(tree_info[z].pos) * Matrix4f::uniformScaleMatrix(tree_info[z].scale) * 
							Matrix4f::rotationAroundZAxis(rng.unitRandom() * Maths::get2Pi<float>());// Matrix4f::scaleMatrix(tree_info[z].width, tree_info[z].width, tree_info[z].height);
						gl_ob->mesh_data = biome_manager->elm_tree_mesh_render_data;
						gl_ob->materials = biome_manager->elm_tree_gl_materials;

						// Randomise leaf colour a little.
						float r_factor = 0.7f + rng.unitRandom() * 0.3f;
						float g_factor = 0.7f + rng.unitRandom() * 0.3f;
						float b_factor = 0.7f + rng.unitRandom() * 0.3f;
						gl_ob->materials[1].albedo_linear_rgb.r *= r_factor;
						gl_ob->materials[1].albedo_linear_rgb.g *= g_factor;
						gl_ob->materials[1].albedo_linear_rgb.b *= b_factor;
						gl_ob->materials[1].transmission_albedo_linear_rgb.r *= r_factor;
						gl_ob->materials[1].transmission_albedo_linear_rgb.g *= g_factor;
						gl_ob->materials[1].transmission_albedo_linear_rgb.b *= b_factor;

						opengl_engine->addObject(gl_ob);
						chunk.gl_obs.push_back(gl_ob);

						//-------------- Create physics tree object --------------
						PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true, biome_manager->elm_tree_physics_shape, /*userdata=*/NULL, /*userdata_type=*/0);
						physics_ob->pos = tree_info[z].pos;
						const float rot_z = 0; // TEMP
						physics_ob->rot = Quatf::fromAxisAndAngle(Vec4f(0,0,1,0), rot_z);
						physics_ob->scale = Vec3f(tree_info[z].scale);

						physics_world->addObject(physics_ob);
						chunk.physics_obs.push_back(physics_ob);
					}
				}
			}
		}

		last_ob_centre_i = centre_i;
		//conPrint("Updating tree ob chunks took " + timer.elapsedString());
	}
#endif

#if 0
	//-------------------------------------------- Far Grass (grass imposters / billboards) --------------------------------------------
	{
	const Vec2i grass_centre(
		Maths::floorToInt(campos.x / GRASS_CHUNK_W),
		Maths::floorToInt(campos.y / GRASS_CHUNK_W));

	if(grass_centre != last_grass_centre)
	{
		const int x0     = grass_centre.x      - GRASS_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = grass_centre.y      - GRASS_CHUNK_GRID_RES/2;
		const int old_x0 = last_grass_centre.x - GRASS_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = last_grass_centre.y - GRASS_CHUNK_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     GRASS_CHUNK_GRID_RES); // x0 mod GRASS_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     GRASS_CHUNK_GRID_RES); // y0 mod GRASS_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, GRASS_CHUNK_GRID_RES);
		const int old_wrapped_y0 = Maths::intMod(old_y0, GRASS_CHUNK_GRID_RES);

		// Iterate over wrapped coordinates
		GrassChunk* const grass_chunks_data = grass_chunks.getData();
		for(int j=0; j<GRASS_CHUNK_GRID_RES; ++j)
		for(int i=0; i<GRASS_CHUNK_GRID_RES; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : GRASS_CHUNK_GRID_RES);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : GRASS_CHUNK_GRID_RES);

			if( old_x < x0 || old_x >= x0 + GRASS_CHUNK_GRID_RES || 
				old_y < y0 || old_y >= y0 + GRASS_CHUNK_GRID_RES)
			{
				// This chunk is out of range of new camera.

				GrassChunk& chunk = grass_chunks_data[i + j * GRASS_CHUNK_GRID_RES];

				if(chunk.grass_gl_ob.nonNull())
					opengl_engine->removeObject(chunk.grass_gl_ob);

				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : GRASS_CHUNK_GRID_RES);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : GRASS_CHUNK_GRID_RES);
				assert(x >= x0 && x < x0 + GRASS_CHUNK_GRID_RES);
				assert(y >= y0 && y < y0 + GRASS_CHUNK_GRID_RES);

				makeGrassChunk(x, y, bump_allocator, chunk); // Sets chunk.tree_info and chunk.imposters_gl_ob.
				if(chunk.grass_gl_ob.nonNull())
					opengl_engine->addObject(chunk.grass_gl_ob);
			}
		}

		last_grass_centre = grass_centre;
		//conPrint("Updating tree ob chunks took " + timer.elapsedString());
	}
	}

	//-------------------------------------------- Near Grass (3d) --------------------------------------------
	{
	const Vec2i grass_centre(
		Maths::floorToInt(campos.x / NEAR_GRASS_CHUNK_W),
		Maths::floorToInt(campos.y / NEAR_GRASS_CHUNK_W));

	if(grass_centre != last_near_grass_centre)
	{
		const int x0     = grass_centre.x           - NEAR_GRASS_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = grass_centre.y           - NEAR_GRASS_CHUNK_GRID_RES/2;
		const int old_x0 = last_near_grass_centre.x - NEAR_GRASS_CHUNK_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = last_near_grass_centre.y - NEAR_GRASS_CHUNK_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     NEAR_GRASS_CHUNK_GRID_RES); // x0 mod NEAR_GRASS_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     NEAR_GRASS_CHUNK_GRID_RES); // y0 mod NEAR_GRASS_CHUNK_GRID_RES                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, NEAR_GRASS_CHUNK_GRID_RES);
		const int old_wrapped_y0 = Maths::intMod(old_y0, NEAR_GRASS_CHUNK_GRID_RES);

		// Iterate over wrapped coordinates
		NearGrassChunk* const near_grass_chunks_data = near_grass_chunks.getData();
		for(int j=0; j<NEAR_GRASS_CHUNK_GRID_RES; ++j)
		for(int i=0; i<NEAR_GRASS_CHUNK_GRID_RES; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : NEAR_GRASS_CHUNK_GRID_RES);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : NEAR_GRASS_CHUNK_GRID_RES);

			if( old_x < x0 || old_x >= x0 + NEAR_GRASS_CHUNK_GRID_RES || 
				old_y < y0 || old_y >= y0 + NEAR_GRASS_CHUNK_GRID_RES)
			{
				// This chunk is out of range of new camera.

				NearGrassChunk& chunk = near_grass_chunks_data[i + j * NEAR_GRASS_CHUNK_GRID_RES];

				if(chunk.grass_gl_ob.nonNull())
					opengl_engine->removeObject(chunk.grass_gl_ob);

				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : NEAR_GRASS_CHUNK_GRID_RES);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : NEAR_GRASS_CHUNK_GRID_RES);
				assert(x >= x0 && x < x0 + NEAR_GRASS_CHUNK_GRID_RES);
				assert(y >= y0 && y < y0 + NEAR_GRASS_CHUNK_GRID_RES);

				makeNearGrassChunk(x, y, bump_allocator, chunk); // Sets chunk.tree_info and chunk.imposters_gl_ob.
				if(chunk.grass_gl_ob.nonNull())
					opengl_engine->addObject(chunk.grass_gl_ob);
			}
		}

		last_near_grass_centre = grass_centre;
		//conPrint("Updating tree ob chunks took " + timer.elapsedString());
	}
	}
#endif

	if(build_imposters_prog.nonNull()) // Mac doesn't support compute shaders, so build_imposters_prog will be null on mac.  Don't do grid scattering on mac.
	{
		Timer timer;
		for(size_t i=0; i<grid_scatters.size(); ++i)
		{
			GridScatter& scatter = *grid_scatters[i];

			updateCamposForGridScatter(campos, bump_allocator, scatter);
		}
		if(timer.elapsed() > 0.00001f)
		{
			//conPrint("Updating grid scatters took " + timer.elapsedStringMSWIthNSigFigs(4));
		}
	}
}


void TerrainScattering::updateCamposForGridScatter(const Vec3d& campos, glare::StackAllocator& bump_allocator, GridScatter& grid_scatter)
{
	const Vec2i centre(
		Maths::floorToInt(campos.x / grid_scatter.chunk_width),
		Maths::floorToInt(campos.y / grid_scatter.chunk_width)
	);

	if(centre != grid_scatter.last_centre)
	{
		const int x0     = centre.x                   - grid_scatter.grid_res/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = centre.y                   - grid_scatter.grid_res/2;
		const int old_x0 = grid_scatter.last_centre.x - grid_scatter.grid_res/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = grid_scatter.last_centre.y - grid_scatter.grid_res/2;
		const int wrapped_x0     = Maths::intMod(x0,     grid_scatter.grid_res); // x0 mod grid_scatter.grid_res                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     grid_scatter.grid_res); // y0 mod grid_scatter.grid_res                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, grid_scatter.grid_res);
		const int old_wrapped_y0 = Maths::intMod(old_y0, grid_scatter.grid_res);

		// Iterate over wrapped coordinates
		GridScatterChunk* const grid_scatter_chunks_data = grid_scatter.chunks.getData();
		for(int j=0; j<grid_scatter.grid_res; ++j)
		for(int i=0; i<grid_scatter.grid_res; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : grid_scatter.grid_res);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : grid_scatter.grid_res);

			if( old_x < x0 || old_x >= x0 + grid_scatter.grid_res || 
				old_y < y0 || old_y >= y0 + grid_scatter.grid_res)
			{
				// This chunk is out of range of new camera.
				GridScatterChunk& chunk = grid_scatter_chunks_data[i + j * grid_scatter.grid_res];

				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : grid_scatter.grid_res);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : grid_scatter.grid_res);
				assert(x >= x0 && x < x0 + grid_scatter.grid_res);
				assert(y >= y0 && y < y0 + grid_scatter.grid_res);

				if(!chunk.imposters_gl_ob)
				{
					makeGridScatterChunk(x, y, bump_allocator, grid_scatter, chunk); // Sets and chunk.imposters_gl_ob.
					if(chunk.imposters_gl_ob)
						opengl_engine->addObject(chunk.imposters_gl_ob);
				}

				updateGridScatterChunkWithComputeShader(x, y, grid_scatter, chunk);
			}
		}

		grid_scatter.last_centre = centre;
		//conPrint("Updating tree ob chunks took " + timer.elapsedString());
	}
}


static inline unsigned int computeHash(int x, int y, unsigned int hash_mask)
{
	// NOTE: technically possible undefined behaviour here (signed overflow)

	return ((x * 73856093) ^ (y * 19349663)) & hash_mask;
}


void TerrainScattering::buildPrecomputedPoints(float chunk_w_m, float density, glare::StackAllocator& bump_allocator, js::Vector<PrecomputedPoint, 16>& precomputed_points)
{
	PCG32 rng(/*initstate=*/1, /*initseq=*/1);

	const float surface_area = chunk_w_m * chunk_w_m;

	const float evenness = 0.6f;

	// If evenness > 0, then min separation distance will be > 0, so some points will be rejected.
	// Generate more candidate points to compensate, so we get a roughly similar amount of accepted points.
	const float overscatter_factor = (float)(1 + 3 * evenness*evenness);

	const int N = (int)std::ceil(surface_area * density * overscatter_factor);

	// We want grid cell width for the hash table to be roughly equal to 2 * the point spacing.
	// A density of d imples an average area per point of 1/d, and hance an average side width per point of sqrt(1/d)
	const float cell_w = (float)(2 / std::sqrt(density));
	const float recip_cell_w = 1.f / cell_w;

	// Increase the minimum distance as evenness increases.
	// Raising evenness to 0.5 here seems to give a decent perceptual 'evenness' of 0.5.
	// The 0.7 factor is to get roughly the same number of accepted points regardless of evenness.
	const float min_dist = 0.7f * (float)sqrt(evenness) / std::sqrt((float)density);

	const float min_dist_2 = min_dist * min_dist;

	const size_t num_buckets = myMax<size_t>(8, Maths::roundToNextHighestPowerOf2((size_t)(N * 1.5f)));

	glare::StackAllocation hashed_points_allocation(num_buckets * sizeof(Vec3f), /*alignment=*/16, bump_allocator);
	Vec3f* const hashed_points = (Vec3f*)hashed_points_allocation.ptr;
	for(size_t i=0; i<num_buckets; ++i)
		hashed_points[i] = Vec3f(std::numeric_limits<float>::infinity());

	const uint32 hash_mask = (uint32)num_buckets - 1;

	precomputed_points.resize(0);
	precomputed_points.reserve(N);

	for(int q=0; q<N; ++q)
	{
		const float u = rng.unitRandom();
		const float v = rng.unitRandom();
		const Vec4f pos(
			u * chunk_w_m,
			v * chunk_w_m,
			0,
			1
		);

		const Vec4i begin = truncateToVec4i((pos - Vec4f(min_dist)) * recip_cell_w);
		const Vec4i end   = truncateToVec4i((pos + Vec4f(min_dist)) * recip_cell_w);

		for(int y = begin[1]; y <= end[1]; ++y)
		for(int x = begin[0]; x <= end[0]; ++x)
		{
			uint32 bucket_i = computeHash(x, y, hash_mask);
			for(int i=0; i<16; ++i)
			{
				const Vec3f& point_in_hash_map = hashed_points[bucket_i];
				if(point_in_hash_map.x == std::numeric_limits<float>::infinity()) // If bucket is free:
					break;
				if(point_in_hash_map.getDist2(Vec3f(pos)) < min_dist_2)
					goto scatterpos_invalid;
				
				bucket_i = (bucket_i + 1) & hash_mask; // Advance to next bucket, wrapping bucket index.
			}
		}

		// Add the point to the hashed grid
		{
			const Vec4i cell_i = truncateToVec4i(pos * recip_cell_w);
			uint32 bucket_i = computeHash(cell_i[0], cell_i[1], hash_mask);

			// Iterate until we find a free bucket
			for(int i=0; i<16; ++i)
			{
				if(hashed_points[bucket_i].x == std::numeric_limits<float>::infinity()) // If bucket is free:
				{
					hashed_points[bucket_i] = Vec3f(pos); // Insert point in bucket
					break;
				}
				bucket_i = (bucket_i + 1) & hash_mask; // Advance to next bucket, wrapping bucket index.
			}
		}

		{
		const float scale_factor =  (rng.unitRandom() * rng.unitRandom() * rng.unitRandom());
		const float rot = rng.unitRandom() * Maths::get2Pi<float>();
		
		PrecomputedPoint point;
		point.uv = Vec2f(u, v);
		point.scale_factor = scale_factor;
		point.rot = rot;
		precomputed_points.push_back(point);
		}

scatterpos_invalid: ; // Null statement for goto label.
	}

	// conPrint("============== Built " + toString(precomputed_points.size()) + " precomputed points");
}


// Compute a list of pseudo-random vegetation positions distributed over the given terrain chunk.
void TerrainScattering::buildVegLocationInfo(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, glare::StackAllocator& bump_allocator, js::Vector<VegetationLocationInfo, 16>& locations_out)
{
	PCG32 rng(/*initstate=*/chunk_x_index, /*initseq=*/chunk_y_index);

	const float surface_area = chunk_w_m * chunk_w_m;

	const float evenness = 0.6f;

	// If evenness > 0, then min separation distance will be > 0, so some points will be rejected.
	// Generate more candidate points to compensate, so we get a roughly similar amount of accepted points.
	const float overscatter_factor = (float)(1 + 3 * evenness*evenness);

	const int N = (int)std::ceil(surface_area * density * overscatter_factor);

	//conPrint("TerrainScattering::buildVegLocationInfo(), N: " + toString(N));

	// We want grid cell width for the hash table to be roughly equal to 2 * the point spacing.
	// A density of d imples an average area per point of 1/d, and hance an average side width per point of sqrt(1/d)
	const float cell_w = (float)(2 / std::sqrt(density));
	const float recip_cell_w = 1.f / cell_w;

	// Increase the minimum distance as evenness increases.
	// Raising evenness to 0.5 here seems to give a decent perceptual 'evenness' of 0.5.
	// The 0.7 factor is to get roughly the same number of accepted points regardless of evenness.
	const float min_dist = 0.7f * (float)sqrt(evenness) / std::sqrt((float)density);

	const float min_dist_2 = min_dist * min_dist;

	const size_t num_buckets = myMax<size_t>(8, Maths::roundToNextHighestPowerOf2((size_t)(N * 1.5f)));

	glare::StackAllocation hashed_points_allocation(num_buckets * sizeof(Vec3f), /*alignment=*/16, bump_allocator);
	Vec3f* const hashed_points = (Vec3f*)hashed_points_allocation.ptr;
	for(size_t i=0; i<num_buckets; ++i)
		hashed_points[i] = Vec3f(std::numeric_limits<float>::infinity());

	const uint32 hash_mask = (uint32)num_buckets - 1;

	locations_out.resize(0);
	locations_out.reserve(N);

	// Work out which detail mask map covers this chunk
	const ImageMapUInt1* detail_mask_map = NULL;
	{
		const int detail_mask_section_x = Maths::floorToInt(((chunk_x_index + 0.5f) * chunk_w_m) / detail_mask_map_width_m) + DETAIL_MASK_MAP_SECTION_RES/2;
		const int detail_mask_section_y = Maths::floorToInt(((chunk_y_index + 0.5f) * chunk_w_m) / detail_mask_map_width_m) + DETAIL_MASK_MAP_SECTION_RES/2;
		if( detail_mask_section_x >= 0 && detail_mask_section_x < DETAIL_MASK_MAP_SECTION_RES &&
			detail_mask_section_y >= 0 && detail_mask_section_y < DETAIL_MASK_MAP_SECTION_RES)
		{
			detail_mask_map = &detail_mask_map_sections[detail_mask_section_x + detail_mask_section_y*DETAIL_MASK_MAP_SECTION_RES].detail_mask_map;
		}
	}

	const TerrainSystem* const terrain_system_  = terrain_system;
	for(int q=0; q<N; ++q)
	{
		const float u = rng.unitRandom();
		const float v = rng.unitRandom();
		const Vec4f pos(
			((float)chunk_x_index + u) * chunk_w_m,
			((float)chunk_y_index + v) * chunk_w_m,
			0,
			1
		);

		// Lookup terrain mask to see if this is vegetation
		const Colour4f mask = terrain_system_->evalTerrainMask(pos[0], pos[1]);
		float vegetation_mask = mask[2];

		// Look up our detail_mask_map, if non-null.
		if(detail_mask_map)
		{
			// Flip y coord since this texture is from OpenGL and hence flipped in y.
			const float val = detail_mask_map->sampleSingleChannelTiled(pos[0] / detail_mask_map_width_m, 1.f - pos[1] / detail_mask_map_width_m, /*channel=*/0);
			vegetation_mask = myMax(vegetation_mask, val);
		}

		if(vegetation_mask < 0.5f)
			continue;

		const float tree_mask = terrain_system_->evalTreeMask(pos[0], pos[1]);
		if(tree_mask < 0.5f)
			continue;

		const Vec4i begin = truncateToVec4i((pos - Vec4f(min_dist)) * recip_cell_w);
		const Vec4i end   = truncateToVec4i((pos + Vec4f(min_dist)) * recip_cell_w);

		for(int y = begin[1]; y <= end[1]; ++y)
		for(int x = begin[0]; x <= end[0]; ++x)
		{
			uint32 bucket_i = computeHash(x, y, hash_mask);
			for(int i=0; i<16; ++i)
			{
				const Vec3f& point_in_hash_map = hashed_points[bucket_i];
				if(point_in_hash_map.x == std::numeric_limits<float>::infinity()) // If bucket is free:
					break;
				if(point_in_hash_map.getDist2(Vec3f(pos)) < min_dist_2)
					goto scatterpos_invalid;
				
				bucket_i = (bucket_i + 1) & hash_mask; // Advance to next bucket, wrapping bucket index.
			}
		}

		// Add the point to the hashed grid
		{
			const Vec4i cell_i = truncateToVec4i(pos * recip_cell_w);
			uint32 bucket_i = computeHash(cell_i[0], cell_i[1], hash_mask);

			// Iterate until we find a free bucket
			for(int i=0; i<16; ++i)
			{
				if(hashed_points[bucket_i].x == std::numeric_limits<float>::infinity()) // If bucket is free:
				{
					hashed_points[bucket_i] = Vec3f(pos); // Insert point in bucket
					break;
				}
				bucket_i = (bucket_i + 1) & hash_mask; // Advance to next bucket, wrapping bucket index.
			}
		}

		{
			//Vec3f(3.f), // base scale
			//Vec3f(1.f), // scale variation

			const float terrain_h = terrain_system_->evalTerrainHeight(pos[0], pos[1], /*quad_w=*/1.f);
			const Vec4f use_pos(pos[0], pos[1], terrain_h/* - 0.1f*/, 1); // NOTE: offsetting down

			//const float base_scale = 3.f;

			const float scale_factor =  (rng.unitRandom() * rng.unitRandom() * rng.unitRandom());
			const float scale_variation = base_scale / 3;//1.f;
			VegetationLocationInfo info;
			info.pos = use_pos;
			//info.width = 9.f + scale_factor * 2.f;
			//info.height = base_height + scale_factor * height_variation;
			info.scale = base_scale + scale_factor * scale_variation;
			locations_out.push_back(info);
		}

scatterpos_invalid: ; // Null statement for goto label.
	}
}


// Compute a list of pseudo-random vegetation positions distributed over the given terrain chunk.
#if 0
void TerrainScattering::buildVegLocationInfoWithPrecomputedPoints(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, glare::StackAllocator& bump_allocator, 
	js::Vector<PrecomputedPoint, 16>& points, js::Vector<VegetationLocationInfo, 16>& locations_out)
{
	locations_out.resize(0);
	locations_out.reserve(points.size());

	for(int q=0; q<points.size(); ++q)
	{
		const float u = points[q].uv.x;
		const float v = points[q].uv.y;
		const Vec4f pos(
			((float)chunk_x_index + u) * chunk_w_m,
			((float)chunk_y_index + v) * chunk_w_m,
			0,
			1
		);

		// Lookup terrain mask to see if this is vegetation
		const Colour4f mask = this->evalTerrainMask(pos[0], pos[1]);
		const float vegetation_mask = mask[2];
		//TEMP if(vegetation_mask < 0.5f)
		//TEMP 	continue;

		const float terrain_h = this->evalTerrainHeight(pos[0], pos[1], /*quad_w=*/1.f);
		const Vec4f use_pos(pos[0], pos[1], terrain_h/* - 0.1f*/, 1); // NOTE: offsetting down

		//const float base_scale = 3.f;

		const float scale_factor =  points[q].scale_factor;
		const float scale_variation = base_scale / 3;//1.f;
		VegetationLocationInfo info;
		info.pos = use_pos;
		//info.width = 9.f + scale_factor * 2.f;
		//info.height = base_height + scale_factor * height_variation;
		info.scale = base_scale + scale_factor * scale_variation;
		locations_out.push_back(info);
	}
}
#endif


GLObjectRef TerrainScattering::buildVegLocationsAndImposterGLOb(int chunk_x_index, int chunk_y_index, float chunk_w_m, float density, float base_scale, float imposter_width_over_height,
	glare::StackAllocator& bump_allocator, js::Vector<PrecomputedPoint, 16>* precomputed_points, js::Vector<VegetationLocationInfo, 16>& locations_out)
{
	//Timer timer;

	// Build list of vegetation positions
	//if(precomputed_points)
	//	buildVegLocationInfoWithPrecomputedPoints(chunk_x_index, chunk_y_index, chunk_w_m, density, base_scale, bump_allocator, *precomputed_points, /*locations_out=*/locations_out);
	//else
		buildVegLocationInfo(chunk_x_index, chunk_y_index, chunk_w_m, density, base_scale, bump_allocator, /*locations_out=*/locations_out);

	//const double build_veg_location_elapsed = timer.elapsed();
	//timer.reset();

	js::Vector<VegetationLocationInfo, 16>& locations = locations_out;
	const int N = (int)locations.size();

	if(N == 0)
		return GLObjectRef(); // If no trees in this chunk, skip building an (empty) imposter GLObject.

	//total_num_trees += N;

	//printVar(total_num_trees);

	OpenGLMeshRenderDataRef mesh_data = new OpenGLMeshRenderData();
	OpenGLMeshRenderData& meshdata = *mesh_data;

	meshdata.setIndexType(GL_UNSIGNED_SHORT);

	meshdata.has_uvs = true;
	meshdata.has_shading_normals = true;
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = N * 6; // 2 tris / imposter * 3 indices / tri
	meshdata.batches[0].prim_start_offset_B = 0;

	meshdata.num_materials_referenced = 1;


	const size_t vert_size_B = sizeof(float) * 7; // position, width, uv, rot

	// NOTE: The order of these attributes should be the same as in OpenGLProgram constructor with the glBindAttribLocations.
	size_t in_vert_offset_B = 0;
	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)vert_size_B;
	pos_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(pos_attrib);
	in_vert_offset_B += sizeof(float) * 3;

	const size_t width_offset_B = in_vert_offset_B;
	VertexAttrib imposter_width_attrib;
	imposter_width_attrib.enabled = true;
	imposter_width_attrib.num_comps = 1;
	imposter_width_attrib.type = GL_FLOAT;
	imposter_width_attrib.normalised = false;
	imposter_width_attrib.stride = (uint32)vert_size_B;
	imposter_width_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(imposter_width_attrib);
	in_vert_offset_B += sizeof(float);

	/*VertexAttrib normal_attrib;
	normal_attrib.enabled = false;
	normal_attrib.num_comps = 4;
	normal_attrib.type = GL_INT_2_10_10_10_REV;
	normal_attrib.normalised = true;
	normal_attrib.stride = (uint32)vert_size_B;
	normal_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(normal_attrib);*/
	//in_vert_offset_B += normal_size_B;

	const size_t uv_offset_B = in_vert_offset_B;
	VertexAttrib uv_attrib;
	uv_attrib.enabled = true;
	uv_attrib.num_comps = 2;
	uv_attrib.type = GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = (uint32)vert_size_B;
	uv_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(uv_attrib);
	in_vert_offset_B += sizeof(float) * 2;

	const size_t imposter_rot_offset_B = in_vert_offset_B;
	VertexAttrib imposter_rot_attrib;
	imposter_rot_attrib.enabled = true;
	imposter_rot_attrib.num_comps = 1;
	imposter_rot_attrib.type = GL_FLOAT;
	imposter_rot_attrib.normalised = false;
	imposter_rot_attrib.stride = (uint32)vert_size_B;
	imposter_rot_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(imposter_rot_attrib);
	in_vert_offset_B += sizeof(float);

	assert(in_vert_offset_B == vert_size_B);


	glare::StackAllocation temp_vert_data(N * 4 * vert_size_B, /*alignment=*/8, bump_allocator);
	uint8* const vert_data = (uint8*)temp_vert_data.ptr;

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	const Vec2f uv0(0,0);
	const Vec2f uv1(1,0);
	const Vec2f uv2(1,1);
	const Vec2f uv3(0,1);
	
	// Build vertex data
	const VegetationLocationInfo* const veg_locations = locations.data();
	for(int i=0; i<N; ++i)
	{
		const float master_scale = 4.5f;
		const float imposter_height = veg_locations[i].scale * master_scale;
		const float imposter_width = imposter_height * imposter_width_over_height;
		const float imposter_rot = 0; // TEMP

		// Store lower 2 vertices
		const Vec4f lower_pos = veg_locations[i].pos;
		
		const Vec4f v0pos = lower_pos;// - Vec4f(0.1f, 0,0,0); // TEMP
		const Vec4f v1pos = lower_pos;// + Vec4f(0.1f, 0,0,0);
		
		// Vertex 0
		std::memcpy(vert_data + vert_size_B * (i * 4 + 0), &v0pos, sizeof(float)*3); // Store x,y,z pos coords.
		std::memcpy(vert_data + vert_size_B * (i * 4 + 0) + width_offset_B , &imposter_width, sizeof(float));
		std::memcpy(vert_data + vert_size_B * (i * 4 + 0) + uv_offset_B , &uv0, sizeof(float)*2);
		std::memcpy(vert_data + vert_size_B * (i * 4 + 0) + imposter_rot_offset_B , &imposter_rot, sizeof(float));

		// Vertex 1
		std::memcpy(vert_data + vert_size_B * (i * 4 + 1), &v1pos, sizeof(float)*3); // Store x,y,z pos coords.
		std::memcpy(vert_data + vert_size_B * (i * 4 + 1) + width_offset_B , &imposter_width, sizeof(float));
		std::memcpy(vert_data + vert_size_B * (i * 4 + 1) + uv_offset_B , &uv1, sizeof(float)*2);
		std::memcpy(vert_data + vert_size_B * (i * 4 + 1) + imposter_rot_offset_B , &imposter_rot, sizeof(float));

		// Store upper 2 vertices
		const Vec4f upper_pos = veg_locations[i].pos + Vec4f(0,0,imposter_height, 0);
		
		const Vec4f v2pos = upper_pos;// + Vec4f(0.1f, 0,0,0); //TEMP
		const Vec4f v3pos = upper_pos;// - Vec4f(0.1f, 0,0,0);

		// Vertex 2
		std::memcpy(vert_data + vert_size_B * (i * 4 + 2), &v2pos, sizeof(float)*3); // Store x,y,z pos coords.
		std::memcpy(vert_data + vert_size_B * (i * 4 + 2) + width_offset_B , &imposter_width, sizeof(float));
		std::memcpy(vert_data + vert_size_B * (i * 4 + 2) + uv_offset_B , &uv2, sizeof(float)*2);
		std::memcpy(vert_data + vert_size_B * (i * 4 + 2) + imposter_rot_offset_B , &imposter_rot, sizeof(float));

		// Vertex 3
		std::memcpy(vert_data + vert_size_B * (i * 4 + 3), &v3pos, sizeof(float)*3); // Store x,y,z pos coords.
		std::memcpy(vert_data + vert_size_B * (i * 4 + 3) + width_offset_B , &imposter_width, sizeof(float));
		std::memcpy(vert_data + vert_size_B * (i * 4 + 3) + uv_offset_B , &uv3, sizeof(float)*2);
		std::memcpy(vert_data + vert_size_B * (i * 4 + 3) + imposter_rot_offset_B , &imposter_rot, sizeof(float));

		aabb_os.enlargeToHoldPoint(lower_pos);
	}

	// Enlarge aabb_os to take into account imposter size
	aabb_os.min_ -= Vec4f(6,6,0,0);
	aabb_os.max_ += Vec4f(6,6,15,0);

	// Build index data
	glare::StackAllocation temp_index_data(sizeof(uint16) * N * 6, /*alignment=*/8, bump_allocator);
	uint16* const indices = (uint16*)temp_index_data.ptr;

	for(int i=0; i<N; ++i)
	{
		// v3   v2
		// |----|
		// |   /|
		// |  / |
		// | /  |
		// |----|--> x
		// v0   v1

		// bot right tri
		const int offset = i * 6;
		indices[offset + 0] = (uint16)(i * 4 + 0); // bot left
		indices[offset + 1] = (uint16)(i * 4 + 1); // bot right
		indices[offset + 2] = (uint16)(i * 4 + 2); // top right

		// top left tri
		indices[offset + 3] = (uint16)(i * 4 + 0); // bot left
		indices[offset + 4] = (uint16)(i * 4 + 2); // top right
		indices[offset + 5] = (uint16)(i * 4 + 3); // top left
	}

	opengl_engine->vert_buf_allocator->allocateBufferSpaceAndVAO(meshdata, mesh_data->vertex_spec, temp_vert_data.ptr, temp_vert_data.size, indices, temp_index_data.size);

	meshdata.aabb_os = aabb_os;


	GLObjectRef gl_ob = opengl_engine->allocateObject();
	gl_ob->ob_to_world_matrix = Matrix4f::identity();
	gl_ob->mesh_data = mesh_data;

	gl_ob->materials.resize(1);
	gl_ob->materials[0].imposter = true;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(162/256.f));
	//gl_ob->materials[0].roughness = 0.f;
	gl_ob->materials[0].fresnel_scale = 0;

	//gl_ob->materials[0].albedo_texture = biome_manager->elm_imposters_tex;
	gl_ob->materials[0].tex_matrix = Matrix2f(1,0,0,-1);
	gl_ob->materials[0].tex_translation = Vec2f(0, 1);


	//const double gl_ob_creation_elapsed = timer.elapsed();
	//conPrint("Built " + toString(N) + " quads, build_veg_location: " + doubleToStringNSigFigs(build_veg_location_elapsed * 1000, 4) + " ms, gl_ob_creation: " + doubleToStringNSigFigs(gl_ob_creation_elapsed * 1000, 4));

	return gl_ob;
}


// Build the imposter ob without setting the vertex data - that will be computed in a compute shader.
GLObjectRef TerrainScattering::makeUninitialisedImposterGLOb(glare::StackAllocator& bump_allocator, const js::Vector<PrecomputedPoint, 16>& precomputed_points)
{
	const int N = (int)precomputed_points.size();

	assert(N > 0);

	OpenGLMeshRenderDataRef mesh_data = new OpenGLMeshRenderData();
	OpenGLMeshRenderData& meshdata = *mesh_data;

	// Set dummy AABB for now.
	// object aabb_ws will be set later in updateGridScatterChunkWithComputeShader().
	meshdata.aabb_os = js::AABBox(Vec4f(-100000.0f,0,0,1), Vec4f(-100000.0f,0,0,1));

	meshdata.setIndexType(GL_UNSIGNED_SHORT);

	meshdata.has_uvs = true;
	meshdata.has_shading_normals = true;
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = N * 6; // 2 tris / imposter * 3 indices / tri
	meshdata.batches[0].prim_start_offset_B = 0;

	meshdata.num_materials_referenced = 1;


	const size_t vert_size_B = sizeof(float) * 7; // position, width, uv, rot

	// NOTE: The order of these attributes should be the same as in OpenGLProgram constructor with the glBindAttribLocations.
	size_t in_vert_offset_B = 0;
	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)vert_size_B;
	pos_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(pos_attrib);
	in_vert_offset_B += sizeof(float) * 3;

	//const size_t width_offset_B = in_vert_offset_B;
	VertexAttrib imposter_width_attrib;
	imposter_width_attrib.enabled = true;
	imposter_width_attrib.num_comps = 1;
	imposter_width_attrib.type = GL_FLOAT;
	imposter_width_attrib.normalised = false;
	imposter_width_attrib.stride = (uint32)vert_size_B;
	imposter_width_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(imposter_width_attrib);
	in_vert_offset_B += sizeof(float);

	//const size_t uv_offset_B = in_vert_offset_B;
	VertexAttrib uv_attrib;
	uv_attrib.enabled = true;
	uv_attrib.num_comps = 2;
	uv_attrib.type = GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = (uint32)vert_size_B;
	uv_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(uv_attrib);
	in_vert_offset_B += sizeof(float) * 2;

	//const size_t imposter_rot_offset_B = in_vert_offset_B;
	VertexAttrib imposter_rot_attrib;
	imposter_rot_attrib.enabled = true;
	imposter_rot_attrib.num_comps = 1;
	imposter_rot_attrib.type = GL_FLOAT;
	imposter_rot_attrib.normalised = false;
	imposter_rot_attrib.stride = (uint32)vert_size_B;
	imposter_rot_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(imposter_rot_attrib);
	in_vert_offset_B += sizeof(float);

	assert(in_vert_offset_B == vert_size_B);

	const size_t total_vert_data_size_B = N * 4 * vert_size_B;
	
	mesh_data->vbo_handle = opengl_engine->vert_buf_allocator->allocateVertexDataSpace(vert_size_B, /*vert data=*/NULL, total_vert_data_size_B);

	// Build index data
	glare::StackAllocation temp_index_data(sizeof(uint16) * N * 6, /*alignment=*/8, bump_allocator);
	uint16* const indices = (uint16*)temp_index_data.ptr;

	for(int i=0; i<N; ++i)
	{
		// v3   v2
		// |----|
		// |   /|
		// |  / |
		// | /  |
		// |----|--> x
		// v0   v1

		// bot right tri
		const int offset = i * 6;
		indices[offset + 0] = (uint16)(i * 4 + 0); // bot left
		indices[offset + 1] = (uint16)(i * 4 + 1); // bot right
		indices[offset + 2] = (uint16)(i * 4 + 2); // top right

		// top left tri
		indices[offset + 3] = (uint16)(i * 4 + 0); // bot left
		indices[offset + 4] = (uint16)(i * 4 + 2); // top right
		indices[offset + 5] = (uint16)(i * 4 + 3); // top left
	}

	meshdata.indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexDataSpace(indices, temp_index_data.size);

	opengl_engine->vert_buf_allocator->getOrCreateAndAssignVAOForMesh(meshdata, meshdata.vertex_spec);

	GLObjectRef gl_ob = opengl_engine->allocateObject();
	gl_ob->ob_to_world_matrix = Matrix4f::identity();
	gl_ob->mesh_data = mesh_data;

	gl_ob->materials.resize(1);
	gl_ob->materials[0].imposter = true;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(162/256.f));
	//gl_ob->materials[0].roughness = 0.f;
	gl_ob->materials[0].fresnel_scale = 0;

	//gl_ob->materials[0].albedo_texture = biome_manager->elm_imposters_tex;
	gl_ob->materials[0].tex_matrix = Matrix2f(1,0,0,-1);
	gl_ob->materials[0].tex_translation = Vec2f(0, 1);

	return gl_ob;
}


//static int total_num_trees = 0;

// Build and sets chunk.tree_info and chunk.imposters_gl_ob
void TerrainScattering::makeTreeChunk(int chunk_x_index, int chunk_y_index, glare::StackAllocator& bump_allocator, LargeTreeChunk& chunk)
{
	const float density = 0.005f;
	const float base_scale = 3.f;
	const float imposter_width_over_height = 0.64f;// Elm imposters are approx 0.64 times as wide as high

	// We will store the locations in the chunk
	chunk.imposters_gl_ob = buildVegLocationsAndImposterGLOb(chunk_x_index, chunk_y_index, LARGE_TREE_CHUNK_W, density, base_scale, imposter_width_over_height, bump_allocator, 
		/*precomputed points=*/NULL, /*locations out=*/chunk.locations);
	if(chunk.imposters_gl_ob.nonNull())
	{
		chunk.imposters_gl_ob->depth_draw_depth_bias = -2.0; // Move position used for depth away from sun by some distance, to avoid shadows from the imposters shadowing the actual tree model, in the transition zone.
		chunk.imposters_gl_ob->materials[0].materialise_lower_z = 100; // begin fade in distance
		chunk.imposters_gl_ob->materials[0].materialise_upper_z = 120; // end fade in distance
		chunk.imposters_gl_ob->materials[0].begin_fade_out_distance = 100000;
		chunk.imposters_gl_ob->materials[0].end_fade_out_distance   = 120000;
		chunk.imposters_gl_ob->materials[0].imposter_tex_has_multiple_angles = true;
		//chunk.imposters_gl_ob->materials[0].materialise_lower_z = 0; // begin fade in distance
		//chunk.imposters_gl_ob->materials[0].materialise_upper_z = 0.01; // end fade in distance
		//chunk.imposters_gl_ob->materials[0].begin_fade_out_distance = 100;
		//chunk.imposters_gl_ob->materials[0].end_fade_out_distance = 120;
		chunk.imposters_gl_ob->materials[0].albedo_texture = biome_manager->elm_imposters_tex;
	}
}

#if 0
void TerrainScattering::makeGrassChunk(int chunk_x_index, int chunk_y_index, glare::StackAllocator& bump_allocator, GrassChunk& chunk)
{
	const float density = 10.f;
	const float base_scale = 0.15f;
	const float imposter_width_over_height = 1.6f;

	chunk.grass_gl_ob = buildVegLocationsAndImposterGLOb(chunk_x_index, chunk_y_index, GRASS_CHUNK_W, density, base_scale, imposter_width_over_height, bump_allocator, 
		/*precomputed points=*/NULL, /*locations out=*/temp_locations);
	if(chunk.grass_gl_ob.nonNull())
	{
		chunk.grass_gl_ob->materials[0].albedo_linear_rgb = Colour3f(1.f);
		chunk.grass_gl_ob->materials[0].materialise_lower_z = 0; // begin fade in distance
		chunk.grass_gl_ob->materials[0].materialise_upper_z = 0.01; // end fade in distance
		chunk.grass_gl_ob->materials[0].begin_fade_out_distance = 3;
		chunk.grass_gl_ob->materials[0].end_fade_out_distance = 4;
		chunk.grass_gl_ob->materials[0].albedo_texture = grass_texture;
	}
}
#endif


void TerrainScattering::makeGridScatterChunk(int chunk_x_index, int chunk_y_index, glare::StackAllocator& bump_allocator, GridScatter& grid_scatter, GridScatterChunk& chunk)
{
	//chunk.imposters_gl_ob = buildVegLocationsAndImposterGLOb(chunk_x_index, chunk_y_index, grid_scatter.chunk_width, grid_scatter.density, grid_scatter.base_scale, grid_scatter.imposter_width_over_height, 
	//	bump_allocator, /*precomputed points=*/&grid_scatter.precomputed_points, /*locations out=*/temp_locations);

	chunk.imposters_gl_ob = makeUninitialisedImposterGLOb(bump_allocator, /*precomputed points=*/grid_scatter.precomputed_points);

	if(chunk.imposters_gl_ob.nonNull())
	{
		chunk.imposters_gl_ob->materials[0].albedo_linear_rgb = Colour3f(1.f);
		chunk.imposters_gl_ob->materials[0].use_wind_vert_shader = grid_scatter.use_wind_vert_shader;
		chunk.imposters_gl_ob->materials[0].materialise_lower_z = grid_scatter.begin_fade_in_distance;
		chunk.imposters_gl_ob->materials[0].materialise_upper_z = grid_scatter.end_fade_in_distance;
		chunk.imposters_gl_ob->materials[0].begin_fade_out_distance = grid_scatter.begin_fade_out_distance;
		chunk.imposters_gl_ob->materials[0].end_fade_out_distance = grid_scatter.end_fade_out_distance;
		chunk.imposters_gl_ob->materials[0].albedo_texture = grid_scatter.imposter_texture;
		chunk.imposters_gl_ob->materials[0].normal_map = grid_scatter.imposter_normal_map;
	}
}


static void bindTextureUnitToSampler(const OpenGLTexture& texture, int texture_unit_index, GLint sampler_uniform_location)
{
	glActiveTexture(GL_TEXTURE0 + texture_unit_index);
	glBindTexture(texture.getTextureTarget(), texture.texture_handle);
	glUniform1i(sampler_uniform_location, texture_unit_index);
}


void TerrainScattering::updateGridScatterChunkWithComputeShader(int chunk_x_index, int chunk_y_index, GridScatter& grid_scatter, GridScatterChunk& chunk)
{
	// See if there is any vegetation on this chunk, by checking all pixels on the mask map that the section covers

	// First, compute terrain section this chunk lies on:
	const float centre_p_x = (chunk_x_index + 0.5f) * grid_scatter.chunk_width; // world space x coordinate in metres at centre of chunk
	const float centre_p_y = (chunk_y_index + 0.5f) * grid_scatter.chunk_width;
	const float centre_nx = centre_p_x * terrain_scale_factor + 0.5f; // Normalised section coordinates of centre of chunk.  Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float centre_ny = centre_p_y * terrain_scale_factor + 0.5f;
	const int section_x = Maths::floorToInt(centre_nx) + TerrainSystem::TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(centre_ny) + TerrainSystem::TERRAIN_SECTION_OFFSET;

	TerrainDataSection* section = NULL;
	float max_vegetation_val = 0;
	if(section_x >= 0 && section_x < TerrainSystem::TERRAIN_DATA_SECTION_RES && section_y >= 0 && section_y < TerrainSystem::TERRAIN_DATA_SECTION_RES)
	{
		section = &terrain_system->terrain_data_sections[section_x + section_y*TerrainSystem::TERRAIN_DATA_SECTION_RES];
		if(section->maskmap.nonNull())
		{
			const float query_nw = grid_scatter.chunk_width / terrain_section_w; // query width in normalised section coordinates
			const int query_w_px = myMax(1, (int)(section->maskmap->getMapWidth() * query_nw)); // in pixels
			const float corner_p_x = chunk_x_index * grid_scatter.chunk_width;
			const float corner_p_y = chunk_y_index * grid_scatter.chunk_width;
			const float corner_nx = Maths::fract(corner_p_x * terrain_scale_factor + 0.5f); // Normalised section coordinates.
			const float corner_ny = Maths::fract(corner_p_y * terrain_scale_factor + 0.5f); // Normalised section coordinates.
			const float step_n = query_nw / query_w_px;
			for(int j=0; j<=query_w_px; ++j)
			for(int i=0; i<=query_w_px; ++i)
			{
				const float nx = corner_nx + (float)i * step_n;
				const float ny = corner_ny + (float)j * step_n;
				assert(nx >= 0.f && nx <= 1.01f);
				assert(ny >= 0.f && ny <= 1.01f);
				const Colour4f mask_val = section->maskmap->vec3Sample(nx, 1.f - ny, /*wrap=*/false);
				max_vegetation_val = myMax(max_vegetation_val, mask_val[2]);
			}
		}
	}


	// Work out which detail mask map (if any) covers this chunk
	const OpenGLTexture* detail_mask_map_gl_tex = NULL;
	{
		const int detail_mask_section_x = Maths::floorToInt(centre_p_x / detail_mask_map_width_m) + DETAIL_MASK_MAP_SECTION_RES/2;
		const int detail_mask_section_y = Maths::floorToInt(centre_p_y / detail_mask_map_width_m) + DETAIL_MASK_MAP_SECTION_RES/2;
		if( detail_mask_section_x >= 0 && detail_mask_section_x < DETAIL_MASK_MAP_SECTION_RES &&
			detail_mask_section_y >= 0 && detail_mask_section_y < DETAIL_MASK_MAP_SECTION_RES)
		{
			const DetailMaskMapSection& detail_section = detail_mask_map_sections[detail_mask_section_x + detail_mask_section_y*DETAIL_MASK_MAP_SECTION_RES];
			if(detail_section.gl_tex_valid)
			{
				detail_mask_map_gl_tex = detail_section.mask_map_gl_tex.ptr();

				const float detail_section_botleft_x_ws = (detail_mask_section_x - DETAIL_MASK_MAP_SECTION_RES/2) * detail_mask_map_width_m;
				const float detail_section_botleft_y_ws = (detail_mask_section_y - DETAIL_MASK_MAP_SECTION_RES/2) * detail_mask_map_width_m;

				const float chunk_botleft_x_ws = chunk_x_index * grid_scatter.chunk_width;
				const float chunk_botleft_y_ws = chunk_y_index * grid_scatter.chunk_width;

				// Compute normalised coordinates inside detail section of the grid chunk botleft.
				const float region_nx = (chunk_botleft_x_ws - detail_section_botleft_x_ws) / detail_mask_map_width_m;
				const float region_ny = (chunk_botleft_y_ws - detail_section_botleft_y_ws) / detail_mask_map_width_m;

				const float region_nw = grid_scatter.chunk_width / detail_mask_map_width_m;

				const bool detail_mask_map_nonzero = detail_section.non_zero_mip_map.isRegionNonZero(region_nx, region_ny, region_nw, region_nw);
				if(detail_mask_map_nonzero)
					max_vegetation_val = 1;
			}
		}
	}

	if(max_vegetation_val == 0)
	{
		if(chunk.imposters_gl_ob->mesh_data->batches[0].num_indices != 0)
		{
			chunk.imposters_gl_ob->mesh_data->batches[0].num_indices = 0; // Disable drawing
			opengl_engine->objectBatchDataChanged(*chunk.imposters_gl_ob);

			chunk.imposters_gl_ob->aabb_ws = js::AABBox(Vec4f(0,0,-1.0e10f, 1), Vec4f(0,0,-1.0e10f, 1));
		}
	}
	else
	{
		if(chunk.imposters_gl_ob->mesh_data->batches[0].num_indices == 0)
		{
			chunk.imposters_gl_ob->mesh_data->batches[0].num_indices = (uint32)grid_scatter.precomputed_points.size() * 6; // (re)enable drawing
			opengl_engine->objectBatchDataChanged(*chunk.imposters_gl_ob);
		}


		//glQueryCounter(timer_query_ids[0], GL_TIMESTAMP); // See http://www.lighthouse3d.com/tutorials/opengl-timer-query/

		build_imposters_prog->useProgram();

		// Update chunk_info_ssbo
		ShaderChunkInfo chunk_info;
		chunk_info.chunk_x_index = chunk_x_index;
		chunk_info.chunk_y_index = chunk_y_index;
		chunk_info.chunk_w_m = grid_scatter.chunk_width;
		chunk_info.section_x = section_x - TerrainSystem::TERRAIN_SECTION_OFFSET; // Set unoffset section_x
		chunk_info.section_y = section_y - TerrainSystem::TERRAIN_SECTION_OFFSET;
		chunk_info.base_scale = grid_scatter.base_scale;
		chunk_info.imposter_width_over_height = grid_scatter.imposter_width_over_height;
		chunk_info.terrain_scale_factor = terrain_scale_factor;
		chunk_info.vert_data_offset_B = (uint32)chunk.imposters_gl_ob->mesh_data->vbo_handle.offset;

		chunk_info_ssbo->updateData(0, &chunk_info, sizeof(chunk_info));

		// Bind stuff

		// Bind heightmap and mask map
		// Work out which source terrain data section we are reading from
		{

		if(section)
		{
			if(section->heightmap_gl_tex.nonNull())
			{
				bindTextureUnitToSampler(*section->heightmap_gl_tex, /*texture unit index=*/0, terrain_height_map_location);
				bindTextureUnitToSampler(*section->mask_gl_tex,      /*texture unit index=*/1, terrain_mask_tex_location);
			}
		}
		}

		bindTextureUnitToSampler(opengl_engine->getFBMTex(), /*texture unit index=*/2, terrain_fbm_tex_location);

		bindTextureUnitToSampler(detail_mask_map_gl_tex ? *detail_mask_map_gl_tex : *default_detail_mask_tex, /*texture unit index=*/3, terrain_detail_mask_tex_location);

#if USE_COMPUTE_SHADER
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, /*binding point=*/VERTEX_DATA_BINDING_POINT_INDEX,        chunk.imposters_gl_ob->mesh_data->vbo_handle.vbo->bufferName());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, /*binding point=*/PRECOMPUTED_POINTS_BINDING_POINT_INDEX, grid_scatter.precomputed_points_ssbo->handle);
	
		// Execute compute shader
		glDispatchCompute(/*num groups x=*/(GLuint)grid_scatter.precomputed_points.size(), 1, 1);
		OpenGLProgram::useNoPrograms();

		glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT); // Make sure all writes to object vertices have finished before use.
#endif

		// Update AABB with approximate bounds
		js::AABBox aabb_ws = js::AABBox::emptyAABBox();
		const int N = 4;
		for(int y=0; y<N; ++y)
		for(int x=0; x<N; ++x)
		{
			const float p_x = (chunk_x_index + x * (1.f / (N - 1))) * grid_scatter.chunk_width;
			const float p_y = (chunk_y_index + y * (1.f / (N - 1))) * grid_scatter.chunk_width;
			const float p_z = terrain_system->evalTerrainHeight(p_x, p_y, 1.0f);
			aabb_ws.enlargeToHoldPoint(Vec4f(p_x, p_y, p_z, 1));
		}

		// Expand AABB a little to handle finite size of imposters, and also terrain bumps/dips between sample positions
		const float padding = 0.3f;
		aabb_ws.min_ -= Vec4f(padding, padding, grid_scatter.chunk_width * 0.2f, 0);
		aabb_ws.max_ += Vec4f(padding, padding, grid_scatter.chunk_width * 0.2f, 0);
	
		chunk.imposters_gl_ob->aabb_ws = aabb_ws;

		chunk.imposters_gl_ob->mesh_data->aabb_os = aabb_ws; // Since we use identity transform

		// TODO: call objectTransformDataChanged() or something if we want to apply local lights to imposters.


		/*glQueryCounter(timer_query_ids[1], GL_TIMESTAMP);

		// wait until the results are available
		GLint stopTimerAvailable = 0;
		while(!stopTimerAvailable)
			glGetQueryObjectiv(timer_query_ids[1], GL_QUERY_RESULT_AVAILABLE, &stopTimerAvailable);

		// get query results
		GLuint64 startTime, stopTime;
		glGetQueryObjectui64v(timer_query_ids[0], GL_QUERY_RESULT, &startTime);
		glGetQueryObjectui64v(timer_query_ids[1], GL_QUERY_RESULT, &stopTime);

		const double elapsed_ms = (stopTime - startTime) / 1000000.0;
		conPrint("updateGridScatterChunkWithComputeShader GPU time: " + doubleToStringNSigFigs(elapsed_ms, 4) + " ms");*/
	}
}


#if 0
void TerrainScattering::makeNearGrassChunk(int chunk_x_index, int chunk_y_index, glare::StackAllocator& bump_allocator, NearGrassChunk& chunk)
{
	const float density = 10.f;
	const float base_scale = 0.1f;

	// Build list of vegetation positions
	buildVegLocationInfo(chunk_x_index, chunk_y_index, NEAR_GRASS_CHUNK_W, density, base_scale, bump_allocator, /*locations_out=*/temp_locations);

	//printVar(temp_locations.size());

	// Make a single 3d model from lots of grass clump models combined
	if(temp_locations.size() > 0)
	{
		Reference<OpenGLMeshRenderData> meshdata = new OpenGLMeshRenderData();
		meshdata->batches = this->grass_clump_meshdata->batches;
		meshdata->has_uvs = this->grass_clump_meshdata->has_uvs;
		meshdata->has_shading_normals = this->grass_clump_meshdata->has_shading_normals;
		meshdata->has_vert_colours = this->grass_clump_meshdata->has_vert_colours;
		meshdata->setIndexType(GL_UNSIGNED_INT); // this->grass_clump_meshdata->getIndexType());
		meshdata->vertex_spec = this->grass_clump_meshdata->vertex_spec;
		meshdata->num_materials_referenced = this->grass_clump_meshdata->num_materials_referenced;

		const size_t num_verts = grass_clump_meshdata->batched_mesh->numVerts();
		const size_t vert_size_B = grass_clump_meshdata->batched_mesh->vertexSize();
		const size_t src_all_verts_size_B = num_verts * vert_size_B;
		const size_t normal_offset = sizeof(Vec3f);
		//const size_t uv_offset = normal_offset + 4;

		glare::BumpAllocation combined_vert_data(src_all_verts_size_B * temp_locations.size(), 16, bump_allocator);

		const uint8*       src_vert_data  = (const uint8*)grass_clump_meshdata->batched_mesh->vertex_data.data();
		      uint8* const dest_vert_data = (uint8*)combined_vert_data.ptr;

		js::AABBox aabb_os = js::AABBox::emptyAABBox();
		for(size_t i=0; i<temp_locations.size(); ++i)
		{
			for(size_t v=0; v<num_verts; ++v)
			{
				// Copy position, offsetting by temp_locations[i].pos
				Vec3f vert_pos;
				std::memcpy(&vert_pos, &src_vert_data[vert_size_B * v], sizeof(Vec3f));

				Vec3f new_vert_pos = vert_pos * (temp_locations[i].scale * 0.5f) + Vec3f(temp_locations[i].pos);
				std::memcpy(&dest_vert_data[i * src_all_verts_size_B + vert_size_B * v], &new_vert_pos, sizeof(Vec3f));

				// Copy packed normal and UV
				std::memcpy(&dest_vert_data[i * src_all_verts_size_B + vert_size_B * v + normal_offset], &src_vert_data[vert_size_B * v + normal_offset], 4 + sizeof(Vec2f));

				// Copy packed normal
				//std::memcpy(&dest_vert_data[i * src_all_verts_size_B + vert_size_B * v + normal_offset], &src_vert_data[vert_size_B * v + normal_offset], 4);

				// Copy UVs
				//std::memcpy(&dest_vert_data[i * src_all_verts_size_B + vert_size_B * v + uv_offset], &src_vert_data[vert_size_B * v + uv_offset], sizeof(Vec2f));

			}

			aabb_os.enlargeToHoldPoint(temp_locations[i].pos);
		}


		const size_t src_num_indices = grass_clump_meshdata->batched_mesh->numIndices();

		glare::BumpAllocation combined_index_data(src_num_indices * temp_locations.size() * sizeof(uint32), 16, bump_allocator);

		const uint16* const src_index_data  = (const uint16*)grass_clump_meshdata->batched_mesh->index_data.data();
		      uint32* const dest_index_data = (uint32*)combined_index_data.ptr;

		for(size_t i=0; i<temp_locations.size(); ++i)
		{
			for(size_t z=0; z<src_num_indices; ++z)
			{
				dest_index_data[i * src_num_indices + z] = (uint32)((size_t)src_index_data[z] + i * num_verts);
			}
		}

		meshdata->indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexData(combined_index_data.ptr, combined_index_data.size);

		meshdata->vbo_handle = opengl_engine->vert_buf_allocator->allocate(meshdata->vertex_spec, combined_vert_data.ptr, combined_vert_data.size);

#if DO_INDIVIDUAL_VAO_ALLOC
		meshdata->individual_vao = new VAO(meshdata->vbo_handle.vbo, meshdata->indices_vbo_handle.index_vbo, meshdata->vertex_spec);
#endif

		// Adjust batch size
		meshdata->batches[0].num_indices = (uint32)(src_num_indices * temp_locations.size());

		// Set AABB
		/*meshdata->aabb_os = js::AABBox(
			Vec4f(chunk_x_index       * NEAR_GRASS_CHUNK_W, chunk_y_index       * NEAR_GRASS_CHUNK_W, -1.0e10f, 1),
			Vec4f((chunk_x_index + 1) * NEAR_GRASS_CHUNK_W, (chunk_y_index + 1) * NEAR_GRASS_CHUNK_W,  1.0e10f, 1)
		);*/
		meshdata->aabb_os = aabb_os;


		GLObjectRef gl_ob = opengl_engine->allocateObject();
		gl_ob->ob_to_world_matrix = Matrix4f::identity();// /*Matrix4f::translationMatrix(temp_locations[0].pos) **/ Matrix4f::uniformScaleMatrix(temp_locations[0].scale * 0.5f) * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
		gl_ob->mesh_data = meshdata;

		gl_ob->materials.resize(1);
		//gl_ob->materials[0].imposterable = true;
		gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(62/255.f, 88/255.f, 22/255.f));
		gl_ob->materials[0].roughness = 0.4f;
		gl_ob->materials[0].fresnel_scale = 0.2f;

		gl_ob->materials[0].tex_matrix = Matrix2f(1,0,0,-1);
		gl_ob->materials[0].tex_translation = Vec2f(0, 1);

		gl_ob->materials[0].imposterable = true;
		gl_ob->materials[0].begin_fade_out_distance = 3.f;
		gl_ob->materials[0].end_fade_out_distance = 4.f;

		chunk.grass_gl_ob = gl_ob;
	}
}
#endif


std::string TerrainScattering::getDiagnostics() const
{
	std::string s;
	s.reserve(512);
	s += "Grass textures GPU RAM: " + getNiceByteSize((grass_texture.nonNull() ? grass_texture->getTotalStorageSizeB() : 0) + (grass_normal_map.nonNull() ? grass_normal_map->getTotalStorageSizeB() : 0)) + "\n";

	size_t detail_mask_map_gpu_mem = 0;
	size_t detail_mask_map_cpu_mem = 0;
	for(int i=0; i<DETAIL_MASK_MAP_SECTION_RES*DETAIL_MASK_MAP_SECTION_RES; ++i)
	{
		if(detail_mask_map_sections[i].mask_map_gl_tex.nonNull())
			detail_mask_map_gpu_mem += detail_mask_map_sections[i].mask_map_gl_tex->getTotalStorageSizeB();

		detail_mask_map_cpu_mem += detail_mask_map_sections[i].detail_mask_map.getByteSize();
	}
	s += "detail mask map GPU RAM: " + getNiceByteSize(detail_mask_map_gpu_mem) + "\n";
	s += "detail mask map CPU RAM: " + getNiceByteSize(detail_mask_map_cpu_mem) + "\n";

	size_t imposter_gpu_mem = 0;
	for(size_t i=0; i<grid_scatters.size(); ++i)
	{
		GridScatter& scatter = *grid_scatters[i];

		for(size_t y=0; y<scatter.chunks.getHeight(); ++y)
		for(size_t x=0; x<scatter.chunks.getWidth() ; ++x)
		{
			GridScatterChunk& chunk = scatter.chunks.elem(x, y);
			if(chunk.imposters_gl_ob.nonNull())
				imposter_gpu_mem += chunk.imposters_gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;
		}
	}
	s += "grass imposter GPU RAM: " + getNiceByteSize(imposter_gpu_mem) + "\n";

	size_t tree_imposter_gpu_mem = 0;
	for(int j=0; j<LARGE_TREE_CHUNK_GRID_RES; ++j)
	for(int i=0; i<LARGE_TREE_CHUNK_GRID_RES; ++i)
	{
		const LargeTreeChunk& chunk = large_tree_chunks.elem(i, j);
		if(chunk.imposters_gl_ob.nonNull())
			tree_imposter_gpu_mem += chunk.imposters_gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;
	}
	s += "tree imposter GPU RAM: " + getNiceByteSize(tree_imposter_gpu_mem) + "\n";

	s += "tree geom GPU RAM: " + getNiceByteSize(biome_manager->elm_tree_mesh_render_data->getTotalMemUsage().geom_gpu_usage) + "\n";

	return s;
}
