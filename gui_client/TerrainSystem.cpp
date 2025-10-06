/*=====================================================================
TerrainSystem.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainSystem.h"


#include "OpenGLEngine.h"
#include "OpenGLShader.h"
#include "BiomeManager.h"
#include "TerrainTests.h"
#include "graphics/PerlinNoise.h"
#include "PhysicsWorld.h"
#include "../shared/ImageDecoding.h"
#include "../shared/WorldSettings.h"
#include <utils/TaskManager.h>
#include <utils/FileUtils.h>
#include <utils/ContainerUtils.h>
#include <utils/RuntimeCheck.h>
#include <utils/PlatformUtils.h>
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


TerrainSystem::TerrainSystem()
{
	num_uncompleted_tasks = 0;
}


TerrainSystem::~TerrainSystem()
{
}


// Pack normal into GL_INT_2_10_10_10_REV format.
inline static uint32 packNormal(const Vec4f& normal)
{
	const Vec4f scaled_normal = normal * 511.f;
	const Vec4i scaled_normal_int = toVec4i(scaled_normal);
	const int x = scaled_normal_int[0];
	const int y = scaled_normal_int[1];
	const int z = scaled_normal_int[2];
	// ANDing with 1023 isolates the bottom 10 bits.
	return (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20);
}


/*
  Consider terrain chunk below, currently at depth 2 in the tree.


      depth 1
----------------------                                 ^
                                                       | morph_end_dist
              depth 2 -> depth 1 transition region     |
                                                       |
  -  -  -  -  -  - -  -                                |  ^
       ______                                          |  |
      |      |    depth 2                              |  |
      |      |                                         |  | morph_start_dist  
      |______|                                         |  |      
         ^                                             |  |    
         |                                             |  |     
         | dist from camera to nearest point in chunk  |  |    
         |                                             |  |  
         v                                             v  v
         *                                                
         Camera                                                

As the nearest point approaches the depth 1 / depth 2 boundary, we want to continuously morph to the lower-detail representation that it will have at depth 1

So we will provide the following information to the vertex shader: the (2d) AABB of the chunk, to compute dist from camera to nearest point in chunk,
as well as the distance from the camera to the transition region (morph_start_dist), for the current depth (depth 2 in this example) and the depth to the depth 1 / depth 2 boundary (morph_end_dist)







screen space angle 

alpha ~= chunk_w / d

where d = ||campos - chunk_centre||

quad_res = 512 / (2 ^ chunk_lod_lvl)

quad_w = 2 ^ chunk_lod_lvl

quad_w_screenspace ~= quad_w / d

= 2 ^ chunk_lod_lvl / d

say we have some target quad_w_screenspace: quad_w_screenspace_target

quad_w_screenspace_target = 2 ^ chunk_lod_lvl / d

2 ^ chunk_lod_lvl = quad_w_screenspace_target * d

chunk_lod_lvl = log_2(quad_w_screenspace_target * d)

also

d = (2 ^ chunk_lod_lvl) / quad_w_screenspace_target


Say d = 1000, quad_w_screenspace_target = 0.001

then chunk_lod_lvl = log_2(0.001 * 1000) = log_2(1) = 0

Say d = 4000, quad_w_screenspace_target = 0.001

then chunk_lod_lvl = log_2(0.001 * 4000) = log_2(4) = 2


----------------------------
chunk_w = world_w / 2^depth

quad_w = chunk_w / res = world_w / (2^depth * res)

quad_w_screenspace ~= quad_w / d = world_w / (2^depth * res * d)

2^depth * res * d * quad_w_screenspace = world_w

2^depth = world_w / (res * d * quad_w_screenspace)

depth = log2(world_w / (res * d * quad_w_screenspace))

----

max depth quad_w = world_w / (chunk_res * 2^max_depth)
= 131072 / (128 * 2^10) = 131072 / (128 * 1024) = 1


*/

static const bool GEOMORPHING_SUPPORT = false;

//static float world_w = 131072;//8192*4;
static float world_w = 32768;//8192*4;   TODO: make this just large enough to enclose all defined terrain sections.
// static float CHUNK_W = 512.f;
static int chunk_res = 127; // quad res per patch
const float quad_w_screenspace_target = 0.032f;
//const float quad_w_screenspace_target = 0.004f;
static const int max_depth = 14;

static const float MAX_PHYSICS_DIST = 500.f; // Build physics objects for terrain chunks if the closest point on them to camera is <= MAX_PHYSICS_DIST away.

//static float world_w = 4096;
//// static float CHUNK_W = 512.f;
//static int chunk_res = 128; // quad res per patch
//const float quad_w_screenspace_target = 0.1f;
////const float quad_w_screenspace_target = 0.004f;
//static const int max_depth = 2;

// Scale factor for world-space -> heightmap UV conversion.
// Its reciprocal is the width of the terrain in metres.
//static const float terrain_section_w = 8 * 1024;
//static const float terrain_scale_factor = 1.f / terrain_section_w;


static Colour3f depth_colours[] = 
{
	Colour3f(1,0,0),
	Colour3f(0,1,0),
	Colour3f(0,0,1),
	Colour3f(1,1,0),
	Colour3f(0,1,1),
	Colour3f(1,0,1),
	Colour3f(0.2f,0.5f,1),
	Colour3f(1,0.5f,0.2f),
	Colour3f(0.5,1,0.5f),
};


typedef ImageMap<uint16, UInt16ComponentValueTraits> ImageMapUInt16;
typedef Reference<ImageMapUInt16> ImageMapUInt16Ref;


// Create index data for chunk, will be reused for all chunks
static IndexBufAllocationHandle createIndexBufferForChunkWithRes(OpenGLEngine* opengl_engine, int vert_res_with_borders)
{
	const int quad_res_with_borders = vert_res_with_borders - 1;

	js::Vector<uint16, 16> vert_index_buffer_uint16(quad_res_with_borders * quad_res_with_borders * 6);
	uint16* const indices = vert_index_buffer_uint16.data();
	for(int y=0; y<quad_res_with_borders; ++y)
	for(int x=0; x<quad_res_with_borders; ++x)
	{
		// Triangulate the quad in this way to match how Jolt triangulates the height field shape.
		// 
		// 
		// |----|
		// | \  |
		// |  \ |
		// |   \|
		// |----|--> x

		// bot left tri
		const int offset = (y*quad_res_with_borders + x) * 6;
		indices[offset + 0] = (uint16)(y       * vert_res_with_borders + x    ); // bot left
		indices[offset + 1] = (uint16)(y       * vert_res_with_borders + x + 1); // bot right
		indices[offset + 2] = (uint16)((y + 1) * vert_res_with_borders + x    ); // top left

		// top right tri
		indices[offset + 3] = (uint16)(y       * vert_res_with_borders + x + 1); // bot right
		indices[offset + 4] = (uint16)((y + 1) * vert_res_with_borders + x + 1); // top right
		indices[offset + 5] = (uint16)((y + 1) * vert_res_with_borders + x    ); // top left
	}

	return opengl_engine->vert_buf_allocator->allocateIndexDataSpace(vert_index_buffer_uint16.data(), vert_index_buffer_uint16.dataSizeBytes());
}


void TerrainSystem::init(const TerrainPathSpec& spec_, const std::string& base_dir_path, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_, BiomeManager* biome_manager_, const Vec3d& campos, glare::TaskManager* task_manager_, 
	glare::StackAllocator& bump_allocator, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_)
{
	spec = spec_;
	opengl_engine = opengl_engine_;
	physics_world = physics_world_;
	biome_manager = biome_manager_;
	task_manager = task_manager_;
	out_msg_queue = out_msg_queue_;

	terrain_section_w = spec_.terrain_section_width_m;
	terrain_scale_factor = 1.f / spec_.terrain_section_width_m;

	next_id = 0;


	ImageMapUInt8Ref default_height_map = new ImageMapUInt8(1, 1, 1);
	default_height_map->getPixel(0, 0)[0] = 0;
	OpenGLTextureRef default_height_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_height_tex__"), *default_height_map);

	ImageMapUInt8Ref default_mask_map = new ImageMapUInt8(1, 1, 4);
	default_mask_map->getPixel(0, 0)[0] = 255;
	default_mask_map->getPixel(0, 0)[1] = 0;
	default_mask_map->getPixel(0, 0)[2] = 0;
	default_mask_map->getPixel(0, 0)[3] = 0;
	OpenGLTextureRef default_mask_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_mask_tex__"), *default_mask_map);


	// Set terrain_data_sections paths from spec
	for(size_t i=0; i<spec.section_specs.size(); ++i)
	{
		TerrainPathSpecSection& section_spec = spec.section_specs[i];
		const int dest_x = section_spec.x + TERRAIN_SECTION_OFFSET;
		const int dest_y = section_spec.y + TERRAIN_SECTION_OFFSET;

		if(dest_x < 0 || dest_x >= TERRAIN_DATA_SECTION_RES ||
			dest_y < 0 || dest_y >= TERRAIN_DATA_SECTION_RES)
		{
			conPrint("Warning: invalid section coords for terrain section spec, section spec will not be used.");
		}
		else
		{
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].heightmap_gl_tex = default_height_tex;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].mask_gl_tex      = default_mask_tex;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].heightmap_path = section_spec.heightmap_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].mask_map_path  = section_spec.mask_map_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].tree_mask_map_path  = section_spec.tree_mask_map_path;
		}
	}

	// Set some default OpenGL terrain textures, to use before proper textures are loaded.
	{
		for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
		for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
			terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES].mask_gl_tex = default_mask_tex;
	}
	{
		//ImageMapUInt8Ref default_detail_col_map = new ImageMapUInt8(1, 1, 4);
		//default_detail_col_map->getPixel(0, 0)[0] = 150;
		//default_detail_col_map->getPixel(0, 0)[1] = 150;
		//default_detail_col_map->getPixel(0, 0)[2] = 150;
		//default_detail_col_map->getPixel(0, 0)[3] = 255;
		//OpenGLTextureRef default_col_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_col_tex__"), *default_detail_col_map);

		//Timer timer;
		OpenGLTextureRef default_col_tex = opengl_engine->getTexture(base_dir_path + "/data/resources/grey_grid.png");
		//conPrint("Loading grid texture took " + timer.elapsedStringNPlaces(4));

		for(int i=0; i<4; ++i)
			opengl_engine->setDetailTexture(i, default_col_tex);
	}

	for(int i=0; i<4; ++i)
		opengl_engine->setDetailHeightmap(i, opengl_engine->dummy_black_tex);


	//detail_heightmap = JPEGDecoder::decode(".", "C:\\Users\\nick\\Downloads\\cgaxis_dirt_with_large_rocks_38_46_4K\\dirt_with_large_rocks_38_46_height.jpg");
	//detail_heightmap = PNGDecoder::decode("D:\\terrain\\GroundPack2\\SAND-08\\tex\\SAND-08-BEACH_DEPTH_2k.png");
	//detail_heightmap = PNGDecoder::decode("D:\\terrain\\GroundPack2\\SAND-11\\tex\\SAND-11-DUNES_DEPTH_2k.png");
	//small_dune_heightmap = PNGDecoder::decode("C:\\Users\\nick\\Downloads\\sand_ground_59_83_height.png");

	
	root_node = new TerrainNode();
	root_node->parent = NULL;
	root_node->aabb = js::AABBox(Vec4f(-world_w/2, -world_w/2, -1, 1), Vec4f(world_w/2, world_w/2, 1, 1));
	root_node->depth = 0;
	root_node->id = next_id++;
	id_to_node_map[root_node->id] = root_node.ptr();
	

	Timer timer;


	// Make material
	terrain_mat.roughness = 1;
	terrain_mat.geomorphing = true;
	//terrain_mat.albedo_texture = opengl_engine->getTexture("D:\\terrain\\colour.png", /*allow_compression=*/false);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("D:\\terrain\\Colormap_0.png", /*allow_compression=*/false);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("N:\\indigo\\trunk\\testscenes\\ColorChecker_sRGB_from_Ref.png", /*allow_compression=*/true);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("C:\\programming\\terraingen\\vs2022_build\\colour.png", /*allow_compression=*/true);
	//terrain_mat.tex_matrix = Matrix2f(1.f/64, 0, 0, -1.f/64);
	terrain_mat.tex_matrix = Matrix2f(terrain_scale_factor, 0, 0, terrain_scale_factor);
	//terrain_mat.tex_translation = Vec2f(0.5f, 0.5f);
	terrain_mat.terrain = true;

	
	water_mat.water = true;

	//conPrint("Terrain init took " + timer.elapsedString() + " (" + toString(num_chunks) + " chunks)");


	//TEMP: create single large water object
	
#if 1
	if(BitUtils::isBitSet(spec.flags, TerrainSpec::WATER_ENABLED_FLAG))
	{
		const float large_water_quad_w = 20000;
		Reference<OpenGLMeshRenderData> quad_meshdata = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), /*res=*/2);
		for(int y=0; y<16; ++y)
		for(int x=0; x<16; ++x)
		{
			const int offset_x = x - 8;
			const int offset_y = y - 8;
			if(!(offset_x == 0 && offset_y == 0))
			{
				// Tessellate ground mesh, to avoid texture shimmer due to large quads.
				GLObjectRef gl_ob = opengl_engine->allocateObject();
				gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, spec.water_z) * Matrix4f::uniformScaleMatrix(large_water_quad_w) * Matrix4f::translationMatrix(-0.5f + offset_x, -0.5f + offset_y, 0);
				gl_ob->mesh_data = quad_meshdata;

				gl_ob->materials.resize(1);
				gl_ob->materials[0].albedo_linear_rgb = Colour3f(1,0,0);
				gl_ob->materials[0] = water_mat;
				opengl_engine->addObject(gl_ob);
				water_gl_obs.push_back(gl_ob);
			}
		}

		{
			// Tessellate ground mesh, to avoid texture shimmer due to large quads.
			GLObjectRef gl_ob = opengl_engine->allocateObject();
			gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, spec.water_z) * Matrix4f::uniformScaleMatrix(large_water_quad_w) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
			gl_ob->mesh_data = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), /*res=*/64);

			gl_ob->materials.resize(1);
			//gl_ob->materials[0].albedo_linear_rgb = Colour3f(0,0,1);
			gl_ob->materials[0] = water_mat;
			opengl_engine->addObject(gl_ob);
			water_gl_obs.push_back(gl_ob);
		}



		// Create cylinder for water boundary
		{
			GLObjectRef gl_ob = opengl_engine->allocateObject();
			const float wall_h = 1000.0f;
			gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -wall_h) * Matrix4f::scaleMatrix(25000, 25000, wall_h);
			gl_ob->mesh_data = MeshPrimitiveBuilding::makeCylinderMesh(*opengl_engine->vert_buf_allocator.ptr(), /*end_caps=*/false);

			gl_ob->materials.resize(1);
			gl_ob->materials[0].albedo_linear_rgb = Colour3f(1,0,0);
			//gl_ob->materials[0] = water_mat;

			opengl_engine->addObject(gl_ob);
			water_gl_obs.push_back(gl_ob);
		}

		opengl_engine->getCurrentScene()->draw_water = true;
		opengl_engine->getCurrentScene()->water_level_z = spec.water_z; // Controls caustic drawing
	}
	else
	{
		opengl_engine->getCurrentScene()->draw_water = false;
		opengl_engine->getCurrentScene()->water_level_z = 0.0; // Controls caustic drawing
	}
#endif


	// Create index data for chunk, will be reused for all chunks
	this->vert_res_10_index_buffer  = createIndexBufferForChunkWithRes(opengl_engine, /*vert_res_with_borders=*/10);
	this->vert_res_130_index_buffer = createIndexBufferForChunkWithRes(opengl_engine, /*vert_res_with_borders=*/130);

	//testTerrainSystem(*this); // TEMP


	terrain_scattering.init(base_dir_path, this, opengl_engine_, physics_world, biome_manager_, campos, bump_allocator);
}


void TerrainSystem::handleTextureLoaded(const OpenGLTextureKey& path, const Map2DRef& map)
{
	// conPrint("TerrainSystem::handleTextureLoaded(): path: '" + path + "'");
	ZoneScoped; // Tracy profiler

	assert(opengl_engine->isOpenGLTextureInsertedForKey(OpenGLTextureKey(path)));

	bool terrain_needs_rebuild = false;

	for(int i=0; i<4; ++i)
	{
		if(spec.detail_col_map_paths[i] == path)
		{
			opengl_engine->setDetailTexture(i, opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path)));
		}

		if(spec.detail_height_map_paths[i] == path)
		{
			opengl_engine->setDetailHeightmap(i, opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path)));

			detail_heightmaps[i] = map;
		}
	}

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];

		if(section.heightmap_path == path)
		{
			section.heightmap = map;
			section.heightmap_gl_tex = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path));
			terrain_needs_rebuild = true;
		}
		if(section.mask_map_path == path)
		{
			section.maskmap = map;
			section.mask_gl_tex = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path));
			terrain_needs_rebuild = true;
		}
		if(section.tree_mask_map_path == path)
		{
			section.treemaskmap = map;
			terrain_needs_rebuild = true;
		}
	}

	if(terrain_needs_rebuild)
	{
		// Reload terrain:
		removeSubtree(root_node.ptr(), root_node->old_subtree_gl_obs, root_node->old_subtree_phys_obs);

		terrain_scattering.rebuild();
	}
}


bool TerrainSystem::isTextureUsedByTerrain(const OpenGLTextureKey& path) const
{
	for(int i=0; i<4; ++i)
	{
		if(spec.detail_col_map_paths[i] == path)
			return true;

		if(spec.detail_height_map_paths[i] == path)
			return true;
	}

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];

		if(section.heightmap_path == path)
			return true;
		if(section.mask_map_path == path)
			return true;
		if(section.tree_mask_map_path == path)
			return true;
	}

	return false;
}


void TerrainSystem::rebuildScattering()
{
	terrain_scattering.rebuild();
}


void TerrainSystem::invalidateVegetationMap(const js::AABBox& aabb_ws)
{
	terrain_scattering.invalidateVegetationMap(aabb_ws);
}


bool TerrainSystem::isTerrainFullyBuilt()
{
	return root_node->subtree_built;
}


// Remove any opengl and physics objects inserted into the opengl and physics engines, in the subtree with given root node.
void TerrainSystem::removeAllNodeDataForSubtree(TerrainNode* node)
{
	if(node->gl_ob.nonNull())
		opengl_engine->removeObject(node->gl_ob);
	if(node->physics_ob.nonNull())
		physics_world->removeObject(node->physics_ob);

	for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
		opengl_engine->removeObject(node->old_subtree_gl_obs[i]);

	for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
		physics_world->removeObject(node->old_subtree_phys_obs[i]);

	for(int i=0; i<4; ++i)
		if(node->children[i].nonNull())
			removeAllNodeDataForSubtree(node->children[i].ptr());
}


void TerrainSystem::shutdown()
{
	// Wait for any MakeTerrainChunkTasks to finish, since they have pointers to this object
	const int max_num_wait_iters = 10000;
	int z = 0;
	assert(num_uncompleted_tasks >= 0);
	while(num_uncompleted_tasks != 0)
	{
		assert(num_uncompleted_tasks >= 0);
		PlatformUtils::Sleep(1);
		z++;
		if(z > max_num_wait_iters)
		{
			conPrint("Internal error: failed to wait for all MakeTerrainChunkTasks: num_uncompleted_tasks=" + toString(num_uncompleted_tasks));
			break;
		}
	}

	terrain_scattering.shutdown();

	if(root_node.nonNull())
		removeAllNodeDataForSubtree(root_node.ptr());
	root_node = NULL;

	id_to_node_map.clear();

	this->vert_res_10_index_buffer = IndexBufAllocationHandle();
	this->vert_res_130_index_buffer = IndexBufAllocationHandle();

	for(size_t i=0; i<water_gl_obs.size(); ++i)
		opengl_engine->removeObject(water_gl_obs[i]);
	water_gl_obs.clear();
}


void TerrainSystem::updateCampos(const Vec3d& campos, glare::StackAllocator& bump_allocator)
{
	updateSubtree(root_node.ptr(), campos);

	terrain_scattering.updateCampos(campos, bump_allocator);
}


/*static void appendSubtreeString(TerrainNode* node, std::string& s)
{
	for(int i=0; i<node->depth; ++i)
		s.push_back(' ');

	s += "node, id " + toString(node->id) + " building: " + boolToString(node->building) + ", subtree_built: " + boolToString(node->subtree_built) + "\n";

	if(node->children[0].nonNull())
	{
		for(int i=0; i<4; ++i)
			appendSubtreeString(node->children[i].ptr(), s);
	}
}*/


struct TerrainSysDiagnosticsInfo
{
	int num_interior_nodes;
	int num_leaf_nodes;
	int max_depth;
	size_t geom_gpu_mem_usage;
	size_t physics_obs_mem_usage;
};

static void processSubtreeDiagnostics(TerrainNode* node, TerrainSysDiagnosticsInfo& info)
{
	info.max_depth = myMax(info.max_depth, node->depth);

	if(node->children[0].nonNull())
	{
		info.num_interior_nodes++;

		for(int i=0; i<4; ++i)
			processSubtreeDiagnostics(node->children[i].ptr(), info);
	}
	else
	{
		info.num_leaf_nodes++;

		if(node->gl_ob.nonNull())         info.geom_gpu_mem_usage += node->gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;
		if(node->pending_gl_ob.nonNull()) info.geom_gpu_mem_usage += node->pending_gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;

		if(node->physics_ob.nonNull())         info.physics_obs_mem_usage += node->physics_ob->shape.size_B;
		if(node->pending_physics_ob.nonNull()) info.physics_obs_mem_usage += node->pending_physics_ob->shape.size_B;

		for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
			info.geom_gpu_mem_usage += node->old_subtree_gl_obs[i]->mesh_data->getTotalMemUsage().geom_gpu_usage;

		for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
			info.physics_obs_mem_usage += node->old_subtree_phys_obs[i]->shape.size_B;
	}
}


std::string TerrainSystem::getDiagnostics() const
{
	/*std::string s;
	if(root_node.nonNull())
		appendSubtreeString(root_node.ptr(), s);
	return s;*/

	TerrainSysDiagnosticsInfo info;
	info.num_interior_nodes = 0;
	info.num_leaf_nodes = 0;
	info.max_depth = 0;
	info.geom_gpu_mem_usage = 0;
	info.physics_obs_mem_usage = 0;

	if(root_node.nonNull())
		processSubtreeDiagnostics(root_node.ptr(), info);

	std::string s = 
		"num interior nodes: " + toString(info.num_interior_nodes) + "\n" +
		"num leaf nodes: " + toString(info.num_leaf_nodes) + "\n" +
		"max depth: " + toString(info.max_depth) + "\n" +
		"geom_gpu_mem_usage: " + getNiceByteSize(info.geom_gpu_mem_usage) + "\n" +
		"physics_obs_mem_usage: " + getNiceByteSize(info.physics_obs_mem_usage) + "\n";


	size_t detail_heightmaps_cpu_mem = 0;
	for(int i=0; i<4; ++i)
		detail_heightmaps_cpu_mem += detail_heightmaps[i].nonNull() ? detail_heightmaps[i]->getByteSize() : 0;

	s += "detail_heightmaps_cpu_mem: " + getNiceByteSize(detail_heightmaps_cpu_mem) + "\n";


	size_t detail_tex_GPU_mem = 0;
	size_t detail_heightmap_GPU_mem = 0;
	for(int i=0; i<4; ++i)
	{
		if(opengl_engine->getDetailTexture(i).nonNull())   detail_tex_GPU_mem += opengl_engine->getDetailTexture(i)->getTotalStorageSizeB();
		if(opengl_engine->getDetailHeightmap(i).nonNull()) detail_heightmap_GPU_mem += opengl_engine->getDetailHeightmap(i)->getTotalStorageSizeB();
	}

	s += "detail tex GPU mem:       " + getNiceByteSize(detail_tex_GPU_mem) + "\n";
	s += "detail heightmap GPU mem: " + getNiceByteSize(detail_heightmap_GPU_mem) + "\n";

	size_t terrain_section_heightmap_CPU_mem = 0;
	size_t terrain_section_maskmap_CPU_mem = 0;
	size_t terrain_section_heightmap_GPU_mem = 0;
	size_t terrain_section_maskmap_GPU_mem = 0;
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];

		if(section.heightmap.nonNull()) terrain_section_heightmap_CPU_mem += section.heightmap->getByteSize();
		if(section.maskmap.nonNull())   terrain_section_maskmap_CPU_mem   += section.maskmap->getByteSize();

		if(section.heightmap_gl_tex.nonNull()) terrain_section_heightmap_GPU_mem += section.heightmap_gl_tex->getTotalStorageSizeB();
		if(section.mask_gl_tex.nonNull())      terrain_section_maskmap_GPU_mem   += section.mask_gl_tex->getTotalStorageSizeB();
	}

	s += "terrain section heightmap CPU mem: " + getNiceByteSize(terrain_section_heightmap_CPU_mem) + "\n";
	s += "terrain section maskmap CPU mem:   " + getNiceByteSize(terrain_section_maskmap_CPU_mem) + "\n";
	s += "terrain section heightmap GPU mem: " + getNiceByteSize(terrain_section_heightmap_GPU_mem) + "\n";
	s += "terrain section maskmap GPU mem:   " + getNiceByteSize(terrain_section_maskmap_GPU_mem) + "\n";

	s += "Terrain scattering:\n" +
		terrain_scattering.getDiagnostics();

	return s;
}


//struct VoronoiBasisNoise01
//{
//	inline static float eval(const Vec4f& p)
//	{
//		Vec4f closest_p;
//		float dist;
//		Voronoi::evaluate3d(p, 1.0f, closest_p, dist);
//		return dist;
//	}
//};


//static inline Vec2f toVec2f(const Vec4f& v)
//{
//	return Vec2f(v[0], v[1]);
//}

static inline float fbm(ImageMapFloat& fbm_imagemap, Vec2f p)
{
	// NOTE: textures are effecively flipped upside down in OpenGL, negate y to compensate.
	return (fbm_imagemap.sampleSingleChannelTiled(p.x, -p.y, 0) - 0.5f) * 2.f;
}

static inline Vec2f rot(Vec2f p)
{
	const float theta = 1.618034 * 3.141592653589 * 2;
	return Vec2f(cos(theta) * p.x - sin(theta) * p.y, sin(theta) * p.x + cos(theta) * p.y);
}

static inline float fbmMix(ImageMapFloat& fbm_imagemap, const Vec2f& p)
{
	return 
		fbm(fbm_imagemap, p) +
		fbm(fbm_imagemap, rot(p * 2)) * 0.5f;
}


// p_x, p_y are world space coordinates.
Colour4f TerrainSystem::evalTerrainMask(float p_x, float p_y) const
{
	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return Colour4f(1,0,0,0);
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	if(section.maskmap.isNull())
		return Colour4f(1,0,0,0);

	return section.maskmap->vec3Sample(nx, 1.f - ny, /*wrap=*/false);
}


// Return value >= 0.5: tree allowed
// p_x, p_y are world space coordinates.
float TerrainSystem::evalTreeMask(float p_x, float p_y) const
{
	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return 1;
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	if(section.treemaskmap.isNull())
		return 1; // If there is no tree mask map, trees are allowed by default.

	return section.treemaskmap->sampleSingleChannelTiled(nx, 1.f - ny, /*channel=*/0);
}


// p_x, p_y are world space coordinates.
float TerrainSystem::evalTerrainHeight(float p_x, float p_y, float quad_w) const
{
#if 1
	const float MIN_TERRAIN_Z = -50.f; // Have a max under-sea depth.  This allows having a flat sea-floor, which in turn allows a lower-res mesh to be used for seafloor chunks.

	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return spec.default_terrain_z;
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	if(section.heightmap.isNull())
		return spec.default_terrain_z;

	const float section_nx = nx - Maths::floorToInt(nx);
	const float section_ny = ny - Maths::floorToInt(ny);


	//const float dist_from_origin = Vec2f(p_x, p_y).length();
	//const float centre_flatten_factor = Maths::smoothStep(700.f, 1000.f, dist_from_origin); // Start the hills only x metres from origin
//	const float x_edge_w = 400;
//	const float centre_flatten_factor_x = Maths::smoothPulse(-600.f - x_edge_w, -600.f, 800.f, 800.f + x_edge_w, p_x); // Start the hills only x metres from origin
//	const float centre_flatten_factor_y = Maths::smoothPulse(-600.f, -500.f, 800.f, 1000.f, p_y); // Start the hills only x metres from origin
//	const float centre_flatten_factor = centre_flatten_factor_x * centre_flatten_factor_y;
//	const float non_flatten_factor = 1 - centre_flatten_factor;


//	const float seaside_factor = Maths::smoothStep(-1000.f, -300.f, p_y);


	const Colour4f mask_val = section.maskmap.nonNull() ? section.maskmap->vec3Sample(section_nx, 1.f - section_ny, /*wrap=*/false) : Colour4f(0.f);
			
	// NOTE: textures are effectively flipped upside down in OpenGL, negate y to compensate.
	const float heightmap_terrain_z = section.heightmap->sampleSingleChannelHighQual(section_nx, 1.f - section_ny, /*channel=*/0, /*wrap=*/false);
	//terrain_h = -300 + seaside_factor * 300 + non_flatten_factor * myMax(MIN_TERRAIN_Z, heightmap_terrain_z);// + detail_h;

	//terrain_h = myMax(MIN_TERRAIN_Z, -300 + seaside_factor * 300 + non_flatten_factor * heightmap_terrain_z);// + detail_h;
	float terrain_h = /*-300 + seaside_factor * 300 +*/ myMax(-100000.f, /*non_flatten_factor **/ heightmap_terrain_z);// + detail_h;

	if(terrain_h > MIN_TERRAIN_Z) // Don't apply fine noise on the seafloor.
	{
		// 
		//const float noise_xy_scale = 1 / 200.f;
		//const Vec4f p = Vec4f(p_x * noise_xy_scale, p_y * noise_xy_scale, 0, 1);
		//const float fbm_val = PerlinNoise::ridgedMultifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/10, /*offset=*/0.1f) * 0.2f;
		//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/10, /*offset=*/0.1f) * 5.5f;
		//const float fbm_val = fbmMix(*opengl_engine->fbm_imagemap, Vec2f(p[0], p[1])) * 1.2f;

		// Vegetation noise
		const float veg_noise_xy_scale = 1 / 50.f;
		const float veg_noise_mag = 0.4f * mask_val[2];
		const float veg_fbm_val = (veg_noise_mag > 0) ?
			fbmMix(*opengl_engine->fbm_imagemap, Vec2f(p_x, p_y) * veg_noise_xy_scale) * veg_noise_mag : 
			0.f;
		terrain_h += veg_fbm_val;

		//const float dune_envelope = Maths::smoothStep(water_z + 0.4f, water_z + 1.5f, terrain_h);
		//const float dune_xy_scale = 1 / 2.f;
		//const float dune_h = 0; // small_dune_heightmap->sampleSingleChannelTiled(p_x * dune_xy_scale, p_y * dune_xy_scale, 0) * dune_envelope * 0.1f;

		//Vec2f detail_uvs = Vec2f(p_x, p_y) * (1 / 3.f);
		//detail_uvs.y *= -1.0;
		Vec2f detail_map_0_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 8.0);
		//Vec2f detail_map_1_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 4.0);
		Vec2f detail_map_2_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 4.0);

			

		float rock_weight_env;
		if(mask_val[0] == 0)
			rock_weight_env = 0;
		else
			rock_weight_env =  Maths::smoothStep(0.2f, 0.6f, mask_val[0] + fbmMix(*opengl_engine->fbm_imagemap, detail_map_2_uvs * 0.2f) * 0.2f);
		float rock_height = detail_heightmaps[0].nonNull() ? detail_heightmaps[0]->sampleSingleChannelTiled(detail_map_0_uvs.x, -detail_map_0_uvs.y, 0) * rock_weight_env : 0;

		//float rock_height = mask_val[0] * 10.0;
			
		//if(mask_val[0] > 0 && detail_heightmaps[0].nonNull())
		//	terrain_h += detail_heightmaps[0]->sampleSingleChannelTiled(detail_map_0_uvs.x, detail_map_0_uvs.y, 0) * mask_val[0] * 1.f;
		//if(mask_val[1] > 0 && detail_heightmaps[1].nonNull())
		//	terrain_h += detail_heightmaps[1]->sampleSingleChannelTiled(detail_map_1_uvs.x, detail_map_1_uvs.y, 0) * mask_val[1] * 0.1f;

		terrain_h += rock_height * 0.8f;
	}
	return terrain_h;

#elif 0
	//	return p_x * 0.01f;
	//	const float xy_scale = 1 / 5.f;


	//const float num_octaves = 10;//-std::log2(quad_w * xy_scale);
//	const Vec4f p = Vec4f(p_x * xy_scale, p_y * xy_scale, 0, 1);
	//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/num_octaves, /*offset=*/0.1f);
	//const float fbm_val = PerlinNoise::noise(p) + PerlinNoise::noise(p * 8.0);
//	const float fbm_val = PerlinNoise::FBM(p, 4);

	const float nx = p_x * scale_factor + 0.5f;
	const float ny = p_y * scale_factor + 0.5f;

	float terrain_h;
	if(nx < 0 || nx > 1 || ny < 0 || ny > 1)
		terrain_h = -300;
	else
		terrain_h = (heightmap->sampleSingleChannelTiledHighQual(nx, ny, 0) - 0.57f) * 800;// + fbm_val * 0.5;
		
	return terrain_h;

	//const float detail_xy_scale = 1 / 3.f;
	//return detail_heightmap->sampleSingleChannelTiled(p_x * detail_xy_scale, p_y * detail_xy_scale, 0) * 0.2f;
#else


//	return PerlinNoise::noise(Vec4f(p_x / 30.f, p_y / 30.f, 0, 0)) * 10.f;
	//return p_x * 0.1f;//PerlinNoise::noise(Vec4f(p_x / 30.f, p_y / 30.f, 0, 0)) * 10.f;
	//return 0.f; // TEMP 
	float seaside_factor = Maths::smoothStep(-1000.f, -300.f, p_y);

	const float dist_from_origin = Vec2f(p_x, p_y).length();
	const float centre_flatten_factor = Maths::smoothStep(700.f, 1000.f, dist_from_origin); // Start the hills only x metres from origin
		
	// FBM feature size is (1 / xy_scale) * 2 ^ -num_octaves     = 1 / (xyscale * 2^num_octaves)
	// For example if xy_scale = 1/3000 and num_octaves = 10, we have feature size = 3000 / 1024 ~= 3.
	// We want the feature size to be = quad_w, e.g.
	// quad_w = (1 / xy_scale) * 2 ^ -num_octaves
	// so
	// quad_w * xy_scale = 2 ^ -num_octaves
	// log2(quad_w * xy_scale) = -num_octaves
	// num_octaves = - log2(quad_w * xy_scale)
	const float xy_scale = 1 / 3000.f;

	const float num_octaves = 10;//-std::log2(quad_w * xy_scale);

	const Vec4f p = Vec4f(p_x * xy_scale, p_y * xy_scale, 0, 1);
	//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/num_octaves, /*offset=*/0.1f);
	const float fbm_val = PerlinNoise::FBM(p, /*num octaves=*/(int)num_octaves);

//	float fbm_val = 0;
//	static float octave_params[] = 
//	{
//		1.0f, 0.5f,
//		2.0f, 0.25f,
//		4.0f, 0.125f,
//		128.0f, 0.04f,
//		256.0f, 0.02f
//	};
//
//	for(int i=0; i<staticArrayNumElems(octave_params)/2; ++i)
//	{
//		const float scale  = octave_params[i*2 + 0];
//		const float weight = octave_params[i*2 + 1];
//		fbm_val += PerlinNoise::noise(p * scale) * weight;
//	}

	//fbm_val += (1 - std::fabs(PerlinNoise::noise(p * 100.f))) * 0.025f; // Ridged noise
	//fbm_val += VoronoiBasisNoise01::eval(p * 0.1f) * 0.1f;//0.025f; // Ridged noise
	//fbm_val += Voronoi::voronoiFBM(toVec2f(p * 10000.f), 2) * 0.01f;

	return -300 + seaside_factor * 300 + centre_flatten_factor * myMax(0.f, fbm_val - 0.2f) * 600;
#endif
}


void TerrainSystem::makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, TerrainChunkData& chunk_data_out) const
{
	//Timer timer;
	/*
	 
	An example mesh with interior_vert_res=4, giving vert_res_with_borders=6
	There will be a 1 quad wide skirt border at the edge of the mesh.
	y
	^ 
	|
	-------------------------
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / | skirt
	| /  | /  | /  | /  | /  |
	|------------------------|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |skirt
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|----> x
	skirt               skirt
	*/

	// Do a quick pass over the data, to see if the heightfield is completely flat here (e.g. is a flat chunk of sea-floor or ground plane).
	bool completely_flat = true;
	{
		const int CHECK_RES = 32;
		const float quad_w = chunk_w / (CHECK_RES - 1);
		const float z_0 = evalTerrainHeight(chunk_x, chunk_y, quad_w);
		for(int y=0; y<CHECK_RES; ++y)
		for(int x=0; x<CHECK_RES; ++x)
		{
			const float p_x = x * quad_w + chunk_x;
			const float p_y = y * quad_w + chunk_y;
			const float z = evalTerrainHeight(p_x, p_y, quad_w);
			if(z != z_0)
			{
				completely_flat = false;
				goto done;
			}
		}
	}
done:

	const int interior_vert_res = completely_flat ? 8 : 128; // Number of vertices along the side of a chunk, excluding the 2 border vertices.  Use a power of 2 for Jolt.
	const int interior_quad_res = interior_vert_res - 1;
	const int vert_res_with_borders = interior_vert_res + 2;
	const int quad_res_with_borders = vert_res_with_borders - 1;

	const float quad_w = chunk_w / interior_quad_res;

	const int jolt_vert_res = interior_vert_res;
	
	Array2D<float> jolt_heightfield;
	if(build_physics_ob)
	{
		jolt_heightfield.resizeNoCopy(jolt_vert_res, jolt_vert_res);
	}

	const size_t normal_size_B = 4;
	size_t vert_size_B = sizeof(Vec3f) + normal_size_B; // position, normal
	if(GEOMORPHING_SUPPORT)
		vert_size_B += sizeof(float) + normal_size_B; // morph-z, morph-normal

	chunk_data_out.vert_res_with_borders = vert_res_with_borders;

	chunk_data_out.mesh_data = new OpenGLMeshRenderData();
	chunk_data_out.mesh_data->vert_data.setAllocator(this->opengl_engine->mem_allocator);
	chunk_data_out.mesh_data->vert_data.resize(vert_size_B * vert_res_with_borders * vert_res_with_borders);
	

	OpenGLMeshRenderData& meshdata = *chunk_data_out.mesh_data;

	meshdata.setIndexType(GL_UNSIGNED_SHORT);

	meshdata.has_uvs = true;
	meshdata.has_shading_normals = true;
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = (uint32)(quad_res_with_borders * quad_res_with_borders * 6);
	meshdata.batches[0].prim_start_offset_B = 0;

	meshdata.num_materials_referenced = 1;

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

	VertexAttrib normal_attrib;
	normal_attrib.enabled = true;
	normal_attrib.num_comps = 4;
	normal_attrib.type = GL_INT_2_10_10_10_REV;
	normal_attrib.normalised = true;
	normal_attrib.stride = (uint32)vert_size_B;
	normal_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(normal_attrib);
	in_vert_offset_B += normal_size_B;

	size_t morph_offset_B, morph_normal_offset_B;
	if(GEOMORPHING_SUPPORT)
	{
		morph_offset_B = in_vert_offset_B;
		VertexAttrib morph_attrib;
		morph_attrib.enabled = true;
		morph_attrib.num_comps = 1;
		morph_attrib.type = GL_FLOAT;
		morph_attrib.normalised = false;
		morph_attrib.stride = (uint32)vert_size_B;
		morph_attrib.offset = (uint32)in_vert_offset_B;
		meshdata.vertex_spec.attributes.push_back(morph_attrib);
		in_vert_offset_B += sizeof(float);

		morph_normal_offset_B = in_vert_offset_B;
		VertexAttrib morph_normal_attrib;
		morph_normal_attrib.enabled = true;
		morph_normal_attrib.num_comps = 4;
		morph_normal_attrib.type = GL_INT_2_10_10_10_REV;
		morph_normal_attrib.normalised = true;
		morph_normal_attrib.stride = (uint32)vert_size_B;
		morph_normal_attrib.offset = (uint32)in_vert_offset_B;
		meshdata.vertex_spec.attributes.push_back(morph_normal_attrib);
		in_vert_offset_B += normal_size_B;
	}

	meshdata.vertex_spec.checkValid();


	assert(in_vert_offset_B == vert_size_B);

	Array2D<float> raw_heightfield(interior_vert_res, interior_vert_res);
	// Array2D<Vec3f> raw_normals(interior_vert_res, interior_vert_res);
	for(int y=0; y<interior_vert_res; ++y)
	for(int x=0; x<interior_vert_res; ++x)
	{
		const float p_x = x * quad_w + chunk_x;
		const float p_y = y * quad_w + chunk_y;
	// 	const float dx = 0.1f;
	// 	const float dy = 0.1f;
	// 
	 	const float z    = evalTerrainHeight(p_x,      p_y,      quad_w); // z = h(p_x, p_y)
	// 	const float z_dx = evalTerrainHeight(p_x + dx, p_y,      quad_w, water); // z_dx = h(p_x + dx, dy)
	// 	const float z_dy = evalTerrainHeight(p_x,      p_y + dy, quad_w, water); // z_dy = h(p_x, p_y + dy)
	// 
	// 	const Vec3f p_dx_minus_p(dx, 0, z_dx - z); // p(p_x + dx, dy) - p(p_x, p_y) = (p_x + dx, d_y, z_dx) - (p_x, p_y, z) = (d_x, 0, z_dx - z)
	// 	const Vec3f p_dy_minus_p(0, dy, z_dy - z);
	// 
	// 	const Vec3f normal = normalise(crossProduct(p_dx_minus_p, p_dy_minus_p));
	// 
	 	raw_heightfield.elem(x, y) = z;
	// 	raw_normals.elem(x, y) = normal;
	}

	//conPrint("eval terrain height took     " + timer.elapsedStringMSWIthNSigFigs(4));
	//timer.reset();

	const float skirt_height = chunk_w * (1 / 128.f) * 0.25f; // The skirt height needs to be large enough to cover any cracks, but smaller is better to avoid wasted fragment drawing.
	const int interior_vert_res_minus_1 = interior_vert_res - 1;
	
	uint8* const vert_data = chunk_data_out.mesh_data->vert_data.data();
	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	for(int y=0; y<vert_res_with_borders; ++y)
	for(int x=0; x<vert_res_with_borders; ++x)
	{
		float p_x; // x coordinate, object space
		int src_x; // x index to use reading from raw_heightfield
		float z_offset = 0;
		if(x == 0) // If edge vert, vert is on bottom of skirt
		{
			p_x = 0;
			src_x = 0;
			z_offset = skirt_height;
		}
		else if(x == vert_res_with_borders-1) // If edge vert, vert is on bottom of skirt
		{
			p_x = interior_vert_res_minus_1 * quad_w;
			src_x = interior_vert_res_minus_1;
			z_offset = skirt_height;
		}
		else
		{
			p_x = (x-1) * quad_w;
			src_x = x-1;
		}

		float p_y; // y coordinate, object space
		int src_y;
		if(y == 0) // If edge vert, vert is on bottom of skirt
		{
			p_y = 0;
			src_y = 0;
			z_offset = skirt_height;
		}
		else if(y == vert_res_with_borders-1) // If edge vert, vert is on bottom of skirt
		{
			p_y = interior_vert_res_minus_1 * quad_w;
			src_y = interior_vert_res_minus_1;
			z_offset = skirt_height;
		}
		else
		{
			p_y = (y-1) * quad_w;
			src_y = y-1;
		}

		const float recip_2_quad_w = 1.f / (quad_w*2);

		// Compute normal and height at vertex.
		// For interior vertices, use central differences from adjacent vertices for computing the normal.
		// This is fast because it avoids calling evalTerrainHeight().
		// For edge vertices, compute normal using evalTerrainHeight() calls, since the resulting normal should match adjacent chunks more closely,
		// for example if the adjacent chunk has different tesselation resolution.
		float h;
		Vec4f normal;
		if(src_x >= 1 && src_x < interior_vert_res_minus_1 && src_y >= 1 && src_y < interior_vert_res_minus_1)
		{
			h = raw_heightfield.elem(src_x, src_y);

			const float dh_dx = (raw_heightfield.elem(src_x+1, src_y) - raw_heightfield.elem(src_x-1, src_y)) * recip_2_quad_w;
			const float dh_dy = (raw_heightfield.elem(src_x, src_y+1) - raw_heightfield.elem(src_x, src_y-1)) * recip_2_quad_w;

			normal = normalise(Vec4f(-dh_dx, -dh_dy, 1, 0));
		}
		else
		{
			// Use large deltas for consistency with the normal generation in the interior of the chunk, otherwise the chunk edge is visible due to different normal generation techniques.
			const float dx = quad_w; 
			const float dy = quad_w;
			
						h    = evalTerrainHeight(chunk_x + p_x,      chunk_y + p_y,      quad_w); // h(p_x, p_y)
			const float h_dx = evalTerrainHeight(chunk_x + p_x + dx, chunk_y + p_y,      quad_w); // h(p_x + dx, dy)
			const float h_dy = evalTerrainHeight(chunk_x + p_x,      chunk_y + p_y + dy, quad_w); // h(p_x, p_y + dy)
			
			const float dh_dx = (h_dx - h) * (1.f / dx);
			const float dh_dy = (h_dy - h) * (1.f / dy);
			
			normal = normalise(Vec4f(-dh_dx, -dh_dy, 1, 0));
		}

		const float p_z = h - z_offset; // Z coordinate taking into account downwards offset for skirt, if applicable.

		if(build_physics_ob)
		{
			const int int_x = x - 1; // Don't include border/skirt vertices in Jolt heightfield.
			const int int_y = y - 1;
			if((int_x >= 0) && (int_x < jolt_vert_res) && (int_y >= 0) && (int_y < jolt_vert_res))
				jolt_heightfield.elem(int_x, jolt_vert_res - 1 - int_y) = p_z;
		}

		const Vec4f pos(p_x, p_y, p_z, 1);
		std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x), &pos, sizeof(float)*3); // Store x,y,z pos coords.

		aabb_os.enlargeToHoldPoint(pos);

		const uint32 packed_normal = packNormal(normal);
		std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + sizeof(float) * 3, &packed_normal, sizeof(uint32));

		// Morph z-displacement:
		// Starred vertices, without the morph displacement, should have the position that the lower LOD level triangle would have, below.
		/*
		 y
		 ^ 
		 |    *         *        (*)
		 |----|----|----|----|----|
		 | \  | \  | \  | \  | \  |
		 |  \ |  \ |  \ |  \ |  \ |
		 |   \|   \|   \|   \|   \|
		*|----|----*----|----*----|
		 | \  | \  | \  | \  | \  |
		 |  \ |  \ |  \ |  \ |  \ |
		 |   \|   \|   \|   \|   \|
		 |---------|----|----|---> x             
		      *         *        (*)
	
		y
		 ^ 
		 |    *         *
		 |---------|---------|
		 | \       | \       |
		 |  \      |  \      |
		 |   \     |   \     |
		*|    \    |    \    |*
		 |     \   |     \   |
		 |      \  |      \  |
		 |       \ |       \ |
		 |---------|---------|---> x
		      *         *
		
			  
		*/

		if(GEOMORPHING_SUPPORT)
		{
			float morphed_z = p_z;
			Vec4f morphed_normal = normal;
			//if((y % 2) == 0)
			//{
			//	if(((x % 2) == 1) && (x + 1 < raw_heightfield.getWidth()))
			//	{
			//		assert(x >= 1 && x + 1 < raw_heightfield.getWidth());
			//		morphed_z      = 0.5f   * (raw_heightfield.elem(x-1, y) + raw_heightfield.elem(x+1, y));
			//		morphed_normal = normalise(raw_normals    .elem(x-1, y) + raw_normals    .elem(x+1, y));
			//	}
			//}
			//else
			//{
			//	if(((x % 2) == 0) && (y + 1 < raw_heightfield.getHeight()))
			//	{
			//		assert(y >= 1 && y + 1 < raw_heightfield.getHeight());
			//		morphed_z      = 0.5f   * (raw_heightfield.elem(x, y-1) + raw_heightfield.elem(x, y+1));
			//		morphed_normal = normalise(raw_normals    .elem(x, y-1) + raw_normals    .elem(x, y+1));
			//	}
			//}
			std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + morph_offset_B,        &morphed_z,           sizeof(float));

			const uint32 packed_morph_normal = packNormal(morphed_normal);
			std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + morph_normal_offset_B, &packed_morph_normal, sizeof(uint32));
		}
	}

	meshdata.aabb_os = aabb_os;

	//conPrint("Creating mesh took           " + timer.elapsedStringMSWIthNSigFigs(4));
	

	if(build_physics_ob)
	{
		//timer.reset();
		
		chunk_data_out.physics_shape = PhysicsWorld::createJoltHeightFieldShape(jolt_vert_res, jolt_heightfield, quad_w);

		//conPrint("Creating physics shape took  " + timer.elapsedStringMSWIthNSigFigs(4));
	}

	//conPrint("---------------");
}


void TerrainSystem::removeLeafGeometry(TerrainNode* node)
{
	if(node->gl_ob.nonNull()) opengl_engine->removeObject(node->gl_ob);
	node->gl_ob = NULL;

	if(node->physics_ob.nonNull()) physics_world->removeObject(node->physics_ob);
	node->physics_ob = NULL;
}


void TerrainSystem::removeSubtree(TerrainNode* node, std::vector<GLObjectRef>& old_subtree_gl_obs_in_out, std::vector<PhysicsObjectRef>& old_subtree_phys_obs_in_out)
{
	ContainerUtils::append(old_subtree_gl_obs_in_out, node->old_subtree_gl_obs);
	ContainerUtils::append(old_subtree_phys_obs_in_out, node->old_subtree_phys_obs);

	if(node->children[0].isNull()) // If this is a leaf node:
	{
		// Remove mesh for leaf node, if any
		//removeLeafGeometry(node);
		if(node->gl_ob.nonNull())
			old_subtree_gl_obs_in_out.push_back(node->gl_ob);
		if(node->physics_ob.nonNull())
			old_subtree_phys_obs_in_out.push_back(node->physics_ob);
	}
	else // Else if this node is an interior node:
	{
		// Remove children
		for(int i=0; i<4; ++i)
		{
			removeSubtree(node->children[i].ptr(), old_subtree_gl_obs_in_out, old_subtree_phys_obs_in_out);
			id_to_node_map.erase(node->children[i]->id);

			if(node->children[i]->vis_aabb_gl_ob.nonNull())
				opengl_engine->removeObject(node->children[i]->vis_aabb_gl_ob);

			node->children[i] = NULL;
		}
	}
}


// The root node of the subtree, 'node', has already been created.
void TerrainSystem::createInteriorNodeSubtree(TerrainNode* node, const Vec3d& campos)
{
	// We should split this node into 4 children, and make it an interior node.
	const float cur_w = node->aabb.max_[0] - node->aabb.min_[0];
	const float child_w = cur_w * 0.5f;

	// bot left child
	node->children[0] = new TerrainNode();
	node->children[0]->parent = node;
	node->children[0]->depth = node->depth + 1;
	node->children[0]->aabb = js::AABBox(node->aabb.min_, node->aabb.max_ - Vec4f(child_w, child_w, 0, 0));

	// bot right child
	node->children[1] = new TerrainNode();
	node->children[1]->parent = node;
	node->children[1]->depth = node->depth + 1;
	node->children[1]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, 0, 0, 0), node->aabb.max_ - Vec4f(0, child_w, 0, 0));

	// top right child
	node->children[2] = new TerrainNode();
	node->children[2]->parent = node;
	node->children[2]->depth = node->depth + 1;
	node->children[2]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, child_w, 0, 0), node->aabb.max_);

	// top left child
	node->children[3] = new TerrainNode();
	node->children[3]->parent = node;
	node->children[3]->depth = node->depth + 1;
	node->children[3]->aabb = js::AABBox(node->aabb.min_ + Vec4f(0, child_w, 0, 0), node->aabb.max_ - Vec4f(child_w, 0, 0, 0));

	// Add an AABB visualisation for debugging
	if(false)
	{
		const Colour3f col = depth_colours[(node->depth + 1) % staticArrayNumElems(depth_colours)];
		for(int i=0; i<4; ++i)
		{
			float padding = 0.01f;
			Vec4f padding_v(padding,padding,padding,0);
			node->children[i]->vis_aabb_gl_ob = opengl_engine->makeAABBObject(node->children[i]->aabb.min_ - padding_v, node->children[i]->aabb.max_ + padding_v, Colour4f(col[0], col[1], col[2], 0.2f));
			opengl_engine->addObject(node->children[i]->vis_aabb_gl_ob);
		}
	}


	// Assign child nodes ids and add to id_to_node_map.
	for(int i=0; i<4; ++i)
	{
		node->children[i]->id = next_id++;
		id_to_node_map[node->children[i]->id] = node->children[i].ptr();
	}

	node->subtree_built = false;

	// Recurse to build child trees
	for(int i=0; i<4; ++i)
		createSubtree(node->children[i].ptr(), campos);
}


static const float USE_MIN_DIST_TO_AABB = 5.f;

static const int LOWER_DEPTH_BOUND = 3; // Enforce some tessellation to make sure each chunk lies completely in only one source terrain section.

// The root node of the subtree, 'node', has already been created.
void TerrainSystem::createSubtree(TerrainNode* node, const Vec3d& campos)
{
	//conPrint("Creating subtree, depth " + toString(node->depth) + ", at " + node->aabb.toStringMaxNDecimalPlaces(4));

	const float min_dist = myMax(USE_MIN_DIST_TO_AABB, node->aabb.distanceToPoint(campos.toVec4fPoint()));

	//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
	// depth = log2(world_w / (res * d * quad_w_screenspace))
	const int desired_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/LOWER_DEPTH_BOUND, /*upperbound=*/max_depth);

	//assert(desired_lod_level <= node->lod_level);
	//assert(desired_depth >= node->depth);

	if(desired_depth > node->depth)
	{
		createInteriorNodeSubtree(node, campos);
	}
	else
	{
		assert(desired_depth <= node->depth);
		// This node should be a leaf node

		assert(num_uncompleted_tasks >= 0);
		num_uncompleted_tasks++;

		// Create geometry for it
		MakeTerrainChunkTask* task = new MakeTerrainChunkTask();
		task->node_id = node->id;
		task->chunk_x = node->aabb.min_[0];
		task->chunk_y = node->aabb.min_[1];
		task->chunk_w = node->aabb.max_[0] - node->aabb.min_[0];
		task->build_physics_ob = min_dist <= MAX_PHYSICS_DIST;
		//task->build_physics_ob = (max_depth - node->depth) < 3;
		task->terrain = this;
		task->out_msg_queue = out_msg_queue;
		task->num_uncompleted_tasks_ptr = &num_uncompleted_tasks;
		task_manager->addTask(task);

		node->building = true;
		node->subtree_built = false;
	}
}


void TerrainSystem::updateSubtree(TerrainNode* cur, const Vec3d& campos)
{
	// We want each leaf node to have lod_level = desired_lod_level for that node

	// Get distance from camera to node

	const float min_dist = myMax(USE_MIN_DIST_TO_AABB, cur->aabb.distanceToPoint(campos.toVec4fPoint()));
	//printVar(min_dist);

	//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
	const int desired_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/LOWER_DEPTH_BOUND, /*upperbound=*/max_depth);

	if(cur->children[0].isNull()) // If 'cur' is a leaf node (has no children, so is not interior node):
	{
		if(desired_depth > cur->depth) // If the desired lod level is greater than the leaf's lod level, we want to split the leaf into 4 child nodes
		{
			// Remove mesh for leaf node, if any
			//removeLeafGeometry(cur);
			// Don't remove leaf geometry yet, wait until subtree geometry is fully built to replace it.
			//cur->num_children_built = 0;
			if(cur->gl_ob.nonNull()) cur->old_subtree_gl_obs.push_back(cur->gl_ob);
			cur->gl_ob = NULL;
			if(cur->physics_ob.nonNull()) cur->old_subtree_phys_obs.push_back(cur->physics_ob);
			cur->physics_ob = NULL;
			
			createSubtree(cur, campos);
		}
	}
	else // Else if 'cur' is an interior node:
	{
		if(desired_depth <= cur->depth) // And it should be a leaf node, or not exist (it is currently too detailed)
		{
			// Change it into a leaf node:

			// Remove children of cur and their subtrees
			for(int i=0; i<4; ++i)
			{
				removeSubtree(cur->children[i].ptr(), cur->old_subtree_gl_obs, cur->old_subtree_phys_obs);
				id_to_node_map.erase(cur->children[i]->id);

				if(cur->children[i]->vis_aabb_gl_ob.nonNull())
					opengl_engine->removeObject(cur->children[i]->vis_aabb_gl_ob);

				cur->children[i] = NULL;
			}
		
			// Start creating geometry for this node:
			// Note that we may already be building geometry for this node, from a previous change from interior node to leaf node.
			// In this case don't make a new task, just wait for existing task.

			assert(cur->gl_ob.isNull());
			if(!cur->building)
			{
				// No chunk at this location, make one
				assert(num_uncompleted_tasks >= 0);
				num_uncompleted_tasks++;

				MakeTerrainChunkTask* task = new MakeTerrainChunkTask();
				task->node_id = cur->id;
				task->chunk_x = cur->aabb.min_[0];
				task->chunk_y = cur->aabb.min_[1];
				task->chunk_w = cur->aabb.max_[0] - cur->aabb.min_[0];
				task->build_physics_ob = min_dist <= MAX_PHYSICS_DIST;
				//task->build_physics_ob = (max_depth - cur->depth) < 3;
				task->terrain = this;
				task->out_msg_queue = out_msg_queue;
				task->num_uncompleted_tasks_ptr = &num_uncompleted_tasks;
				task_manager->addTask(task);

				//conPrint("Making new node chunk");

				cur->subtree_built = false;
				cur->building = true;
			}
		}
		else // Else if 'cur' should still be an interior node:
		{
			assert(cur->children[0].nonNull());
			for(int i=0; i<4; ++i)
				updateSubtree(cur->children[i].ptr(), campos);
		}
	}
}


// The subtree with root node 'node' is fully built, so we can remove any old meshes for it, and insert the new pending meshes.
void TerrainSystem::insertPendingMeshesForSubtree(TerrainNode* node)
{
	// Remove any old subtree GL obs and physics obs, now the mesh for this node is ready.
	for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
		opengl_engine->removeObject(node->old_subtree_gl_obs[i]);
	node->old_subtree_gl_obs.clear();

	for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
		physics_world->removeObject(node->old_subtree_phys_obs[i]);
	node->old_subtree_phys_obs.clear();


	if(node->children[0].isNull()) // If leaf node:
	{
		if(node->pending_gl_ob.nonNull())
		{
//FAILING			assert(node->gl_ob.isNull());
			node->gl_ob = node->pending_gl_ob;
			opengl_engine->addObject(node->gl_ob);
			node->pending_gl_ob = NULL;
		}

		if(node->pending_physics_ob.nonNull())
		{
			//			assert(node->physics_ob.isNull());
			node->physics_ob = node->pending_physics_ob;
			physics_world->addObject(node->physics_ob);
			node->pending_physics_ob = NULL;
		}
	}
	else
	{
		for(int i=0; i<4; ++i)
			insertPendingMeshesForSubtree(node->children[i].ptr());
	}
}


/*

When node a is subdivided into 4 (or more) nodes;
set a counter, num_children_built on node a to zero.
whenever node b, c, d, e is built, walk up tree to parent (a), and increment num_children_built.
When it reaches 4, this means that all children are built.  In that case remove the gl ob from node a, and add all gl obs in the subtrees of node a.

                                  a
 a          =>                    |_______________
                                  |    |    |     |
                                  b    c    d     e

                                  a
 a          =>                    |_______________
                                  |    |    |     |
                                  b    c    d     e
                                       |____________
                                       |     |     |
                                       f     g     h


A subtree with root node n is complete if all leaf nodes in the subtree are built.


When an interior node is changed into a leaf node (e.g. children are removed), remove children but add a list of their gl objects to their parent (old_subtree_gl_obs).

a
|_______________             =>                a
|    |    |     |
b    c    d     e

e.g. when b, c, d, e are removed, add list of their gl objects to node a.   When node a is built, remove gl objects in old_subtree_gl_obs from world, and add node 'a' gl object.

*/


bool TerrainSystem::areAllParentSubtreesBuilt(TerrainNode* node)
{
	TerrainNode* cur = node->parent;
	while(cur)
	{
		if(!cur->subtree_built)
			return false;
		cur = cur->parent;
	}

	return true;
}



void TerrainSystem::handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg)
{
	//Timer timer;

	// Lookup node based on id
	auto res = id_to_node_map.find(msg.node_id);
	if(res != id_to_node_map.end())
	{
		TerrainNode& node = *res->second;

		node.building = false;
		if(node.children[0].nonNull()) // If this is an interior node:
			return; // Discard the obsolete built mesh.  This will happen if a leaf node gets converted to an interior node while the mesh is building.

		// This node is a leaf node, and we have the mesh for it, therefore the subtree is complete.
		node.subtree_built = true;

		
		Reference<OpenGLMeshRenderData> mesh_data = msg.chunk_data.mesh_data;

		// Update node AABB, now that we have actual heighfield data.
		// Offset node object space AABB by the chunk x, y coords to get the world-space AABB.
		node.aabb = js::AABBox(
			mesh_data->aabb_os.min_ + Vec4f(msg.chunk_x, msg.chunk_y, 0, 0),
			mesh_data->aabb_os.max_ + Vec4f(msg.chunk_x, msg.chunk_y, 0, 0)
		);

		{
			//printVar(mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			//printVar(mesh_data->vert_data.dataSizeBytes());

			//if(!mesh_data->vert_index_buffer.empty())
			//	mesh_data->indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer.data(), mesh_data->vert_index_buffer.dataSizeBytes());
			//else
			//	mesh_data->indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer_uint16.data(), mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			assert(msg.chunk_data.vert_res_with_borders == 10 || msg.chunk_data.vert_res_with_borders == 130);
			if(msg.chunk_data.vert_res_with_borders == 10)
				mesh_data->indices_vbo_handle = this->vert_res_10_index_buffer;
			else if(msg.chunk_data.vert_res_with_borders == 130)
				mesh_data->indices_vbo_handle = this->vert_res_130_index_buffer;
			else
				conPrint("Erropr. invalid msg.chunk_data.vert_res_with_borders");

			mesh_data->vbo_handle = opengl_engine->vert_buf_allocator->allocateVertexDataSpace(mesh_data->vertex_spec.vertStride(), mesh_data->vert_data.data(), mesh_data->vert_data.dataSizeBytes());

			opengl_engine->vert_buf_allocator->getOrCreateAndAssignVAOForMesh(*mesh_data, mesh_data->vertex_spec);

			// Now data has been uploaded to GPU, clear CPU mem
			mesh_data->vert_data.clearAndFreeMem();
			mesh_data->vert_index_buffer.clearAndFreeMem();
			mesh_data->vert_index_buffer_uint16.clearAndFreeMem();
			mesh_data->vert_index_buffer_uint8.clearAndFreeMem();
		}

		GLObjectRef gl_ob = opengl_engine->allocateObject();
		gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(msg.chunk_x, msg.chunk_y, 0);
		gl_ob->mesh_data = mesh_data;

		// d = (2 ^ chunk_lod_lvl) / quad_w_screenspace_target
		//const float lod_transition_dist = (1 << max_lod_level) / quad_w_screenspace_target;
		/*chunk.gl_ob->morph_start_dist = lod_transition_dist;
		chunk.gl_ob->morph_end_dist = lod_transition_dist * 1.02f;*/
		//gl_ob->aabb_min_x = node.aabb.min_[0];
		//gl_ob->aabb_min_y = node.aabb.min_[1];
		//gl_ob->aabb_w = node.aabb.max_[0] - node.aabb.min_[0];

		// Compute distance at which this node will transition to a smaller depth value (node.depth - 1).
		//
		// From above:
		// depth = log2(world_w / (res * d * quad_w_screenspace))
		// 2^depth = world_w / (res * d * quad_w_screenspace);
		// res * d * quad_w_screenspace * 2^depth = world_w
		// d = world_w / (res * quad_w_screenspace * 2^depth)
		const float transition_depth = world_w / (chunk_res * quad_w_screenspace_target * (1 << node.depth));
		//printVar(transition_depth);

		gl_ob->morph_start_dist = transition_depth * 0.75f;
		gl_ob->morph_end_dist   = transition_depth;

		//printVar(gl_ob->morph_start_dist);
		//printVar(gl_ob->morph_end_dist);


		gl_ob->materials.resize(1);
		gl_ob->materials[0] = terrain_mat;
		//assert(node.depth >= 0 && node.depth < staticArrayNumElems(depth_colours));
		//gl_ob->materials[0].albedo_linear_rgb = depth_colours[node.depth % staticArrayNumElems(depth_colours)];

		// Assign mask map as diffuse texture, based on which source section the chunk lies in.
		const float chunk_middle_x = msg.chunk_x + msg.chunk_w/2; // world space x coord in middle of chunk
		const float chunk_middle_y = msg.chunk_y + msg.chunk_w/2;

		const int section_x = Maths::floorToInt(chunk_middle_x / terrain_section_w + 0.5); // section indices
		const int section_y = Maths::floorToInt(chunk_middle_y / terrain_section_w + 0.5);

		const int index_x = section_x + TERRAIN_SECTION_OFFSET; // Indices into terrain_data_sections array
		const int index_y = section_y + TERRAIN_SECTION_OFFSET;

		if(index_x >= 0 && index_x < 8 && index_y >= 0 && index_y < 8)
		{
			const TerrainDataSection& section = terrain_data_sections[index_x + index_y * TERRAIN_DATA_SECTION_RES];
			gl_ob->materials[0].albedo_texture = section.mask_gl_tex;
		}

		// Since we are doing texture clamping, we need to make sure tex coords are mapped to [0, 1] with tex matrix
		// For example, section (1, 2), will have section_x = 1 and section_y = 2.
		// Its uvs are world space coordinates, so will be something like (8192 * 1, 8192 * 2) at corner, before texture matrix multiplication.
		// After multiplciation is (1, 2).  We want to translate that down to (0, 0), so we want to translate by (-1, -2)
		gl_ob->materials[0].tex_translation = Vec2f(-(float)section_x, -(float)section_y);


		PhysicsShape shape = msg.chunk_data.physics_shape;

		PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
		physics_ob->shape = shape;
		physics_ob->pos = Vec4f(msg.chunk_x, msg.chunk_y, 0, 1);
		physics_ob->rot = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
		physics_ob->scale = Vec3f(1.f);

		physics_ob->kinematic = false;
		physics_ob->dynamic = false;

		node.pending_gl_ob = gl_ob;
		node.pending_physics_ob = physics_ob;


		if(areAllParentSubtreesBuilt(&node))
		{
			insertPendingMeshesForSubtree(&node);
		}
		else
		{
			TerrainNode* cur = node.parent;
			while(cur)
			{
				bool cur_subtree_built = true;
				for(int i=0; i<4; ++i)
					if(!cur->children[i]->subtree_built)
					{
						cur_subtree_built = false;
						break;
					}

				if(!cur->subtree_built && cur_subtree_built)
				{
					// If cur subtree was not built before, and now it is:

					if(areAllParentSubtreesBuilt(cur))
					{
						insertPendingMeshesForSubtree(cur);
					}

					cur->subtree_built = true;
				}

				if(!cur_subtree_built)
					break;

				cur = cur->parent;
			}
		}
	}

	//conPrint("TerrainSystem::handleCompletedMakeChunkTask() took " + timer.elapsedString());
}


void MakeTerrainChunkTask::run(size_t thread_index)
{
	try
	{
		assert((*num_uncompleted_tasks_ptr) >= 0);

		// Make terrain
		terrain->makeTerrainChunkMesh(chunk_x, chunk_y, chunk_w, build_physics_ob, /*chunk data out=*/chunk_data);

		// Send message to out-message-queue (e.g. to MainWindow), saying that we have finished the work.
		TerrainChunkGeneratedMsg* msg = new TerrainChunkGeneratedMsg();
		msg->chunk_x = chunk_x;
		msg->chunk_y = chunk_y;
		msg->chunk_w = chunk_w;
		//msg->lod_level = lod_level;
		msg->chunk_data = chunk_data;
		msg->node_id = node_id;
		out_msg_queue->enqueue(msg);

		// Make water
		//TerrainSystem::makeTerrainChunkMesh(chunk_x_i, chunk_y_i, lod_level, /*water=*/true, chunk_data);
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	assert((*num_uncompleted_tasks_ptr) >= 0);
	(*num_uncompleted_tasks_ptr)--;
}


void MakeTerrainChunkTask::removedFromQueue()
{
	assert((*num_uncompleted_tasks_ptr) >= 0);
	(*num_uncompleted_tasks_ptr)--;
}
