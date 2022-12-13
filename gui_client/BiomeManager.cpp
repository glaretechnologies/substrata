/*=====================================================================
BiomeManager.cpp
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "BiomeManager.h"


#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include "MeshManager.h"
#include "WorldState.h"
#include "PhysicsObject.h"
#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include "simpleraytracer/raymesh.h"
#include "simpleraytracer/ray.h"
#include "physics/HashedGrid.h"
#include "maths/PCG32.h"
#include <Exception.h>
#include <RuntimeCheck.h>
#include <Parser.h>


BiomeManager::BiomeManager()
{
}


void BiomeManager::clear(OpenGLEngine& opengl_engine, PhysicsWorld& physics_world)
{
	for(auto it = ob_to_biome_data.begin(); it != ob_to_biome_data.end(); ++it)
	{
		Reference<ObBiomeData> data = it->second;

		for(size_t i=0; i<data->opengl_obs.size(); ++i)
			opengl_engine.removeObject(data->opengl_obs[i]);

		for(size_t i=0; i<data->physics_objects.size(); ++i)
			physics_world.removeObject(data->physics_objects[i]);
	}

	ob_to_biome_data.clear();


	//for(size_t i=0; i<opengl_obs.size(); ++i)
	//	opengl_engine.removeObject(opengl_obs[i]);
	//opengl_obs.clear();
	//
	//for(auto it = park_biome_physics_objects.begin(); it != park_biome_physics_objects.end(); ++it)
	//	physics_world.removeObject(*it);
	park_biome_physics_objects.clear();


	for(auto it = patches_a.begin(); it != patches_a.end(); ++it)
		for(size_t t=0; t<it->second.opengl_obs.size(); ++t)
			opengl_engine.removeObject(it->second.opengl_obs[t]);
	patches_a.clear();

	for(auto it = patches_b.begin(); it != patches_b.end(); ++it)
		for(size_t t=0; t<it->second.opengl_obs.size(); ++t)
			opengl_engine.removeObject(it->second.opengl_obs[t]);
	patches_b.clear();

	for(auto it = patches_c.begin(); it != patches_c.end(); ++it)
		for(size_t t=0; t<it->second.opengl_obs.size(); ++t)
			opengl_engine.removeObject(it->second.opengl_obs[t]);
	patches_c.clear();
}


void BiomeManager::initTexturesAndModels(const std::string& base_dir_path, OpenGLEngine& opengl_engine, ResourceManager& resource_manager)
{
	if(!resource_manager.isFileForURLPresent("elm_RT_glb_3393252396927074015.bmesh"))
		resource_manager.copyLocalFileToResourceDir(base_dir_path + "/resources/elm_RT_glb_3393252396927074015.bmesh", "elm_RT_glb_3393252396927074015.bmesh");
	if(!resource_manager.isFileForURLPresent("Quad_obj_17249492137259942610.bmesh"))
		resource_manager.copyLocalFileToResourceDir(base_dir_path + "/resources/Quad_obj_17249492137259942610.bmesh", "Quad_obj_17249492137259942610.bmesh");
//	if(!resource_manager.isFileForURLPresent("grass_2819211535648845788.bmesh"))
//		resource_manager.copyLocalFileToResourceDir(base_dir_path + "/resources/grass_2819211535648845788.bmesh", "grass_2819211535648845788.bmesh");

	if(elm_imposters_tex.isNull())
		elm_imposters_tex = opengl_engine.getTexture(base_dir_path + "/resources/imposters/elm_imposters.png");
	
	if(elm_leaf_tex.isNull())
		elm_leaf_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_leaf_frontface.png");
	
	if(elm_leaf_backface_tex.isNull())
		elm_leaf_backface_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_leaf_backface.png");

	if(elm_leaf_transmission_tex.isNull())
		elm_leaf_transmission_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_leaf_transmission.png");

	/*if(elm_leaf_tex.isNull())
		elm_leaf_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_branch_backface.png");

	if(elm_leaf_backface_tex.isNull())
		elm_leaf_backface_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_branch_basecolor.png"); // NOTE: tor seems to have switched these

	if(elm_leaf_transmission_tex.isNull())
		elm_leaf_transmission_tex = opengl_engine.getTexture(base_dir_path + "/resources/elm_branch_transmission.png");*/

//	if(grass_tex.isNull())
//		grass_tex = opengl_engine.getTexture(base_dir_path + "/resources/grass.png");
}


static const Matrix4f instanceObToWorldMatrix(const Vec4f& pos, float rot_z, const Vec3f& scale)
{
	// Equivalent to
	//return Matrix4f::translationMatrix(pos + ob.translation) *
	//	Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle) *
	//	Matrix4f::scaleMatrix(use_scale.x, use_scale.y, use_scale.z));

	Matrix4f rot = Matrix4f::rotationAroundZAxis(rot_z);
	rot.setColumn(0, rot.getColumn(0) * scale.x);
	rot.setColumn(1, rot.getColumn(1) * scale.y);
	rot.setColumn(2, rot.getColumn(2) * scale.z);
	rot.setColumn(3, pos);
	return rot;
}


// Adds to instances parameter, updates instances_aabb_ws_in_out
static void addScatteredObjects(WorldObject& world_ob, WorldState& world_state, PhysicsWorld& physics_world, MeshManager& mesh_manager, glare::TaskManager& task_manager, OpenGLEngine& opengl_engine,
	ResourceManager& resource_manager, GLObjectRef prototype_ob, PhysicsShape physics_shape, float density, float evenness, bool add_physics_objects, const Vec3f& base_scale, const Vec3f& scale_variation, float z_offset,
	const js::Vector<js::AABBox, 16> no_scatter_aabbs, // Don't place instances in these volumes
	js::Vector<BiomeObInstance, 16>& instances, js::AABBox& instances_aabb_ws_in_out)
{
	runtimeCheck(world_ob.scattering_info.nonNull());

	Timer timer;

	const float surface_area = world_ob.scattering_info->total_surface_area * 0.5f; // NOTE: assuming cube, ignoring underfaces.

	// If evenness > 0, then min separation distance will be > 0, so some points will be rejected.
	// Generate more candidate points to compensate, so we get a roughly similar amount of accepted points.
	const float overscatter_factor = (float)(1 + 3 * evenness*evenness);

	const int N = (int)std::ceil(surface_area * density * overscatter_factor);

	PCG32 rng(/*initstate=*/1, /*initseq=*/1);

	// We want grid cell width for the hash table to be roughly equal to 2 * the point spacing.
	// A density of d imples an average area per point of 1/d, and hance an average side width per point of sqrt(1/d)
	const float cell_w = (float)(2 / std::sqrt(density));

	// Increase the minimum distance as evenness increases.
	// Raising evenness to 0.5 here seems to give a decent perceptual 'evenness' of 0.5.
	// The 0.7 factor is to get roughly the same number of accepted points regardless of evenness.
	const float min_dist = 0.7f * (float)sqrt(evenness) / std::sqrt((float)density);

	const float min_dist_2 = min_dist * min_dist;

	HashedGrid<Vec3f> hashed_grid(world_ob.scattering_info->aabb_ws, cell_w,
		N // Expected num items
	);

	// Compute AABB over all instances of the object
	js::AABBox all_instances_aabb_ws = js::AABBox::emptyAABBox();

	const Matrix4f to_world_matrix = obToWorldMatrix(world_ob);

	//const size_t initial_num_instances = instances.size();
	for(int i=0; i<N; ++i)
	{
		const float u1 = rng.unitRandom();
		const float u = rng.unitRandom();
		const float v = rng.unitRandom();

		// Pick sub-element
		float sub_elem_prob;
		const uint32 sub_elem_index = world_ob.scattering_info->uniform_dist.sample(u1, sub_elem_prob);

		Vec4f pos_os, N_g_os;
		unsigned int material_index;
		Vec2f uv0;
		HitInfo hitinfo;
		world_ob.scattering_info->raymesh->sampleSubElement(sub_elem_index, SamplePair(u, v), pos_os, N_g_os, hitinfo, material_index, uv0);

		const Vec4f scatterpos = to_world_matrix * pos_os;
		const Vec3f scatterpos_vec3(scatterpos[0], scatterpos[1], scatterpos[2]);

		bool scatterpos_valid = true;

		for(size_t z=0; z<no_scatter_aabbs.size(); ++z)
			if(no_scatter_aabbs[z].contains(scatterpos))
			{
				scatterpos_valid = false;
				break;
			}
		if(!scatterpos_valid)
			continue;
		
		const Vec4i begin = hashed_grid.getGridMinBound(scatterpos - Vec4f(min_dist, min_dist, min_dist, 0));
		const Vec4i end   = hashed_grid.getGridMaxBound(scatterpos + Vec4f(min_dist, min_dist, min_dist, 0));

		for(int z = begin[2]; z <= end[2]; ++z)
		for(int y = begin[1]; y <= end[1]; ++y)
		for(int x = begin[0]; x <= end[0]; ++x)
		{
			const HashBucket<Vec3f>& bucket = hashed_grid.getBucketForIndices(x, y, z);
			for(size_t q=0; q<bucket.data.size(); ++q)
			{
				const float dist2 = bucket.data[q].getDist2(scatterpos_vec3);
				if(dist2 < min_dist_2)
				{
					scatterpos_valid = false;
					goto finished_looping;
				}
			}
		}
finished_looping:
		if(!scatterpos_valid)
			continue;

		hashed_grid.insert(scatterpos_vec3, scatterpos); // Add the point to the hashed grid


		BiomeObInstance instance;
		const Vec4f pos = scatterpos + Vec4f(0, 0, z_offset, 0);
		const float rot_z = Maths::get2Pi<float>() * rng.unitRandom();
		const Vec3f scale = base_scale + (rng.unitRandom() * rng.unitRandom() * rng.unitRandom()) * scale_variation;
		instance.to_world = instanceObToWorldMatrix(pos, rot_z, scale);// * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
		instance.to_world_no_rot = instanceObToWorldMatrix(pos, 0.f, scale);// * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());

		instances.push_back(instance);

		// Make physics object
		if(add_physics_objects)
		{
			PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true,
				physics_shape, // geometry
				NULL, // userdata
				0 // userdata_type
			);
			physics_ob->pos = pos;
			physics_ob->rot = Quatf::fromAxisAndAngle(Vec4f(0,0,1,0), rot_z);
			physics_ob->scale = scale;

			physics_world.addObject(physics_ob);
		}

		all_instances_aabb_ws.enlargeToHoldAABBox(prototype_ob->mesh_data->aabb_os.transformedAABBFast(instance.to_world));
	}

	// const size_t num_instances_added = instances.size() - initial_num_instances;
	// conPrint("Added " + toString(num_instances_added) + " out of " + toString(N) + " candidate points (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");

	instances_aabb_ws_in_out.enlargeToHoldAABBox(all_instances_aabb_ws);
}


#if 0
static GLObjectRef makeGrassOb(VertexBufferAllocator& vert_buf_allocator, MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager, OpenGLTextureRef grass_tex)
{
	std::vector<WorldMaterialRef> materials(1);
	materials[0] = new WorldMaterial();
	materials[0]->colour_rgb = Colour3f(1.f);
	materials[0]->roughness.val = 0.8f;
	materials[0]->flags = WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;
	materials[0]->tex_matrix = Matrix2f(1, 0, 0, -1); // Y coord needs to be flipped on leaf texture for some reason.

	RayMeshRef raymesh;
	Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeGLMeshDataAndRayMeshForModelURL("grass_2819211535648845788.bmesh", resource_manager, task_manager, &vert_buf_allocator, /*skip opengl calls=*/false, raymesh);

	GLObjectRef grass_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(gl_meshdata, /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, /*ob to world matrix=*/Matrix4f::identity());

	//GLObjectRef grass_ob = ModelLoading::makeGLObjectForModelURLAndMaterials("grass_2819211535648845788.bmesh"/*"Quad_obj_17249492137259942610.bmesh"*/, /*ob lod level=*/0, materials, /*lightmap URL=*/"", 
	//	resource_manager, /*mesh_manager, */task_manager, &vert_buf_allocator,
	//	/*ob to world matrix=*/Matrix4f::identity(), /*skip opengl calls=*/false, raymesh);

	for(size_t i=0; i<grass_ob->materials.size(); ++i)
	{
		grass_ob->materials[i].imposterable = true; // Fade out with distance
		grass_ob->materials[i].begin_fade_out_distance = 45.f;
		grass_ob->materials[i].end_fade_out_distance = 60.f;
		grass_ob->materials[i].double_sided = true;
		grass_ob->materials[i].use_wind_vert_shader = true;
	}

	assert(0);
	//if(grass_ob->mesh_data->vert_vbo.isNull()) // If this data has not been loaded into OpenGL yet:
	//	OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(*grass_ob->mesh_data); // Load mesh data into OpenGL

	grass_ob->materials[0].albedo_texture = grass_tex;
	grass_ob->materials[0].backface_albedo_texture = grass_tex;
	grass_ob->materials[0].transmission_texture = grass_tex;
	//grass_ob->is_imposter = true;
	//grass_ob->is_instanced_ob_with_imposters = true;

	return grass_ob;
}
#endif


GLObjectRef BiomeManager::makeElmTreeOb(OpenGLEngine& opengl_engine, VertexBufferAllocator& vert_buf_allocator, MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager, PhysicsShape& physics_shape_out)
{
	std::vector<WorldMaterialRef> materials(2);
	materials[0] = new WorldMaterial();
	materials[0]->colour_rgb = Colour3f(162/256.f);
	materials[0]->colour_texture_url = "GLB_image_11255090336016867094_jpg_11255090336016867094.jpg"; // Tree trunk texture
	materials[0]->tex_matrix = Matrix2f(1, 0, 0, -1); // Y coord needs to be flipped on leaf texture for some reason.
	materials[0]->roughness.val = 1.f;
	materials[1] = new WorldMaterial();
	materials[1]->colour_rgb = Colour3f(162/256.f); // TEMP different
	materials[1]->colour_texture_url = "elm_leaf_new_png_17162787394814938526.png";
	materials[1]->tex_matrix = Matrix2f(1, 0, 0, -1); // Y coord needs to be flipped on leaf texture for some reason.

	if(elm_tree_mesh_data.isNull())
	{
		const std::string model_URL = "elm_RT_glb_3393252396927074015.bmesh";

		elm_tree_mesh_data = mesh_manager.getMeshData(model_URL);
		if(elm_tree_mesh_data.isNull())
		{
			PhysicsShape physics_shape;
			BatchedMeshRef batched_mesh;
			Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeGLMeshDataAndRayMeshForModelURL(model_URL, resource_manager, task_manager,
				&vert_buf_allocator, /*skip opengl calls=*/false, physics_shape, batched_mesh);

			// Add to mesh manager
			elm_tree_mesh_data = mesh_manager.insertMeshes(model_URL, gl_meshdata, physics_shape);
		}
	}

	GLObjectRef tree_opengl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(opengl_engine, elm_tree_mesh_data->gl_meshdata, /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, 
		/*ob to world matrix=*/Matrix4f::identity());

	// Do assignedLoadedOpenGLTexturesToMats() equivalent
	tree_opengl_ob->materials[0].albedo_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(resource_manager.pathForURL(materials[0]->colour_texture_url)), /*use_sRGB=*/true);

	physics_shape_out = elm_tree_mesh_data->physics_shape;

	for(size_t i=0; i<tree_opengl_ob->materials.size(); ++i)
	{
		tree_opengl_ob->materials[i].imposterable = true; // Mark mats as imposterable so they can smoothly blend out
		tree_opengl_ob->materials[i].use_wind_vert_shader = true;
	}

	tree_opengl_ob->materials[1].double_sided = true;
	tree_opengl_ob->materials[1].albedo_texture = elm_leaf_tex;
	tree_opengl_ob->materials[1].backface_albedo_texture = elm_leaf_backface_tex;
	tree_opengl_ob->materials[1].transmission_texture = elm_leaf_transmission_tex;


	if(!tree_opengl_ob->mesh_data->vbo_handle.valid()) // If this data has not been loaded into OpenGL yet:
		OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(vert_buf_allocator, *tree_opengl_ob->mesh_data); // Load mesh data into OpenGL

	tree_opengl_ob->is_instanced_ob_with_imposters = true;

	return tree_opengl_ob;
}


GLObjectRef BiomeManager::makeElmTreeImposterOb(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator, MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager/*, OpenGLTextureRef elm_imposters_tex*/)
{
	std::vector<WorldMaterialRef> materials(1);
	materials[0] = new WorldMaterial();
	materials[0]->colour_rgb = Colour3f(162/256.f);
	materials[0]->roughness.val = 0.f;
	materials[0]->flags = WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;

	if(elm_tree_imposter_mesh_data.isNull())
	{
		const std::string model_URL = "Quad_obj_17249492137259942610.bmesh";

		elm_tree_imposter_mesh_data = mesh_manager.getMeshData(model_URL);
		if(elm_tree_imposter_mesh_data.isNull())
		{
			PhysicsShape physics_shape;
			BatchedMeshRef batched_mesh;
			Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeGLMeshDataAndRayMeshForModelURL(model_URL, resource_manager, task_manager,
				&vert_buf_allocator, /*skip opengl calls=*/false, physics_shape, batched_mesh);

			// Add to mesh manager
			elm_tree_imposter_mesh_data = mesh_manager.insertMeshes(model_URL, gl_meshdata, physics_shape);
		}
	}

	GLObjectRef tree_imposter_opengl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(gl_engine, elm_tree_imposter_mesh_data->gl_meshdata, /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, 
		/*ob to world matrix=*/Matrix4f::identity());

	for(size_t i=0; i<tree_imposter_opengl_ob->materials.size(); ++i)
		tree_imposter_opengl_ob->materials[i].imposter = true; // Mark mats as imposters so they use the imposter shader

	if(!tree_imposter_opengl_ob->mesh_data->vbo_handle.valid()) // If this data has not been loaded into OpenGL yet:
		OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(vert_buf_allocator, *tree_imposter_opengl_ob->mesh_data); // Load mesh data into OpenGL

	tree_imposter_opengl_ob->materials[0].albedo_texture = elm_imposters_tex;
	tree_imposter_opengl_ob->is_imposter = true;
	tree_imposter_opengl_ob->is_instanced_ob_with_imposters = true;

	return tree_imposter_opengl_ob;
}


// Throws glare::Exception
#if 0
static void parseBiomeInfo(const std::string& ob_content, js::Vector<js::AABBox, 16>& no_tree_aabbs_out)
{
	Parser parser(ob_content);
	if(!parser.parseCString("biome: park"))
		throw glare::Exception("Expected biome: park");
	parser.parseWhiteSpace();
	while(!parser.eof())
	{
		if(parser.parseCString("no_tree_area"))
		{
			float x1, y1, x2, y2;
			parser.parseWhiteSpace();
			if(!parser.parseFloat(x1))
				throw glare::Exception("Expected float");
			parser.parseWhiteSpace();
			if(!parser.parseFloat(y1))
				throw glare::Exception("Expected float");
			parser.parseWhiteSpace();
			if(!parser.parseFloat(x2))
				throw glare::Exception("Expected float");
			parser.parseWhiteSpace();
			if(!parser.parseFloat(y2))
				throw glare::Exception("Expected float");
			parser.parseWhiteSpace();

			no_tree_aabbs_out.push_back(js::AABBox(Vec4f(x1, y1, -1000, 1), Vec4f(x2, y2, 1000, 1)));
		}
		else
			throw glare::Exception("Invalid token");
	}
}
#endif


void BiomeManager::addObjectToBiome(WorldObject& world_ob, WorldState& world_state, PhysicsWorld& physics_world, MeshManager& mesh_manager, glare::TaskManager& task_manager, OpenGLEngine& opengl_engine,
	ResourceManager& resource_manager)
{
	//if(grass_ob.isNull())
	//	grass_ob = makeGrassOb(mesh_manager, task_manager, resource_manager, grass_tex);

	runtimeCheck(world_ob.scattering_info.nonNull());

	if(hasPrefix(world_ob.content, "biome: park"))
	{
		// Parse no tree areas
		js::Vector<js::AABBox, 16> no_tree_aabbs;

		// This code below breaks existing builds that require "biome: park" to be the complete string, not just a prefix.  So don't use for now.
		/*try
		{
			parseBiomeInfo(world_ob.content, no_tree_aabbs);
		}
		catch(glare::Exception& e)
		{
			conPrint("BiomeManager::addObjectToBiome: parse error: " + e.what());
		}*/

		// TEMP HACK: Hack in some no-tree areas to avoid the monorail.
		if(world_ob.uid.value() == 152105)
		{
			no_tree_aabbs.push_back(js::AABBox(Vec4f(606, -144, -1000, 1), Vec4f(631, -126, 1000, 1)));
			no_tree_aabbs.push_back(js::AABBox(Vec4f(633, -127, -1000, 1), Vec4f(639, -14.8, 1000, 1)));
			no_tree_aabbs.push_back(js::AABBox(Vec4f(605, -4, -1000, 1), Vec4f(627, 3, 1000, 1)));
		}
		else if(world_ob.uid.value() == 152064)
		{
			no_tree_aabbs.push_back(js::AABBox(Vec4f(-194, 302, -1000, 1), Vec4f(-142, 310, 1000, 1)));
		}



		park_biome_physics_objects.push_back(world_ob.physics_object);
		Reference<ObBiomeData> ob_biome_date = new ObBiomeData();
		ob_to_biome_data[&world_ob] = ob_biome_date;

		if(true) // Scatter trees:
		{

		PhysicsShape physics_shape;
		GLObjectRef tree_opengl_ob = makeElmTreeOb(opengl_engine, *opengl_engine.vert_buf_allocator, mesh_manager, task_manager, resource_manager, physics_shape);

		// Compute some tree instance points
		// Scatter elm tree
		js::Vector<BiomeObInstance, 16> ob_instances;
		js::AABBox ob_trees_aabb_ws = js::AABBox::emptyAABBox();
		addScatteredObjects(world_ob, world_state, physics_world, mesh_manager, task_manager, opengl_engine, resource_manager, tree_opengl_ob, physics_shape,
			0.005f, // density
			0.6f, // evenness
			true, // add_physics_objects
			Vec3f(3.f), // base scale
			Vec3f(1.f), // scale variation
			0.f, // z offset
			no_tree_aabbs,
			ob_instances, // tree instances in/out
			ob_trees_aabb_ws // instances_aabb_ws_in_out
		);

		tree_opengl_ob->instance_info.resize(ob_instances.size());//tree_opengl_ob->instance_info.size() + tree_instances.size());

		const js::AABBox tree_aabb_os = physics_shape.getAABBOS();
		js::Vector<Matrix4f, 16> instance_matrices(ob_instances.size());
		for(size_t z=0; z<ob_instances.size(); ++z)
		{
			instance_matrices[z] = ob_instances[z].to_world;

			tree_opengl_ob->instance_info[z].to_world = ob_instances[z].to_world;
			tree_opengl_ob->instance_info[z].aabb_ws = tree_aabb_os.transformedAABBFast(ob_instances[z].to_world);
		}

		tree_opengl_ob->enableInstancing(*opengl_engine.vert_buf_allocator, instance_matrices.data(), sizeof(Matrix4f) * instance_matrices.size());
		
		opengl_engine.addObject(tree_opengl_ob);
		tree_opengl_ob->aabb_ws = ob_trees_aabb_ws; // override AABB with AABB of all instances



		// Add the imposter instances to the opengl engine as well
		GLObjectRef tree_imposter_opengl_ob = makeElmTreeImposterOb(opengl_engine, *opengl_engine.vert_buf_allocator, mesh_manager, task_manager, resource_manager/*, elm_imposters_tex*/);

		tree_imposter_opengl_ob->instance_info.resize(ob_instances.size());
		js::Vector<Matrix4f, 16> imposter_matrices(ob_instances.size());
		for(size_t z=0; z<ob_instances.size(); ++z)
		{
			// Elm imposters are approx 0.64 times as wide as high
			const float master_scale = 4.5;
			imposter_matrices[z] = Matrix4f::translationMatrix(0,0,master_scale * 1.3f) * ob_instances[z].to_world_no_rot * Matrix4f::scaleMatrix(master_scale, master_scale * 0.64, master_scale); //Matrix4f::translationMatrix(tree_instances[z].pos + Vec4f(0,0,5.5,0)) * Matrix4f::uniformScaleMatrix(14);// * Matrix4f::rotationAroundZAxis(-Maths::pi_2<float>());

			tree_imposter_opengl_ob->instance_info[z].to_world = imposter_matrices[z];
			tree_imposter_opengl_ob->instance_info[z].aabb_ws = tree_aabb_os.transformedAABBFast(ob_instances[z].to_world_no_rot); // NOTE: use OS AABB of actual tree object for computing the WS AABB.
		}
		tree_imposter_opengl_ob->enableInstancing(*opengl_engine.vert_buf_allocator, imposter_matrices.data(), sizeof(Matrix4f) * imposter_matrices.size());

		opengl_engine.addObject(tree_imposter_opengl_ob);
		tree_imposter_opengl_ob->aabb_ws = ob_trees_aabb_ws; // override AABB with AABB of all instances

		ob_biome_date->opengl_obs.push_back(tree_opengl_ob);
		ob_biome_date->opengl_obs.push_back(tree_imposter_opengl_ob);
		//this->opengl_obs.push_back(tree_opengl_ob);
		//this->opengl_obs.push_back(tree_imposter_opengl_ob);
		}
	}
	//else if(false)//world_ob.content == "biome: grass")
	//{
	//	world_ob.physics_object->buildUniformSampler(); // Check built.
	//
	//	// Scatter grass
	//	{
	//		std::vector<WorldMaterialRef> materials(1);
	//		materials[0] = new WorldMaterial();
	//		materials[0]->colour_rgb = Colour3f(1.0f);
	//		materials[0]->colour_texture_url = "sgrass5_1_modified3_png_10724620288307369837.png";
	//		materials[0]->roughness.val = 0.f;
	//
	//
	//		RayMeshRef raymesh;
	//		GLObjectRef grass_opengl_ob = ModelLoading::makeGLObjectForModelURLAndMaterials("Quad_obj_17249492137259942610.bmesh", /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, mesh_manager, task_manager, 
	//			/*ob to world matrix=*/Matrix4f::identity(), /*skip opengl calls=*/false, raymesh);
	//
	//		if(grass_opengl_ob->mesh_data->vert_vbo.isNull()) // If this data has not been loaded into OpenGL yet:
	//			OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(*grass_opengl_ob->mesh_data); // Load mesh data into OpenGL
	//
	//		addScatteredObject(world_ob, world_state, physics_world, mesh_manager, task_manager, opengl_engine, resource_manager, grass_opengl_ob, raymesh,
	//			30000, // N
	//			0.1f, // min dist
	//			false, // add_physics_objects
	//			Vec3f(2.5f, 1.f, 1.f), // base scale
	//			Vec3f(0.f, 0.f, 0.3f), // scale variation
	//			0.1f // z offset
	//		);
	//	}
	//}
}


void BiomeManager::updatePatchSet(std::map<Vec2i, Patch>& patches, float patch_w, const Vec4f& campos, const Vec4f& cam_forwards_ws, const Vec4f& cam_right_ws, const Vec4f& sundir, OpenGLEngine& opengl_engine)
{
	const int cur_x = Maths::floorToInt(campos[0] / patch_w);
	const int cur_y = Maths::floorToInt(campos[1] / patch_w);

	const int r = 2;
	const int NUM_NEW_QUADS = (1 + 2*r)*(1 + 2*r);
	Vec2i new_quads[NUM_NEW_QUADS];
	int qi=0;
	for(int x=cur_x - r; x <= cur_x + r; ++x)
		for(int y=cur_y - r; y <= cur_y + r; ++y)
		{
			new_quads[qi++] = Vec2i(x, y);
		}
	assert(qi == NUM_NEW_QUADS);

	PCG32 rng(1);
	const int N = 2000; // Max num scattered instances per patch
	instance_matrices_temp.resize(N);

	// We will do a mark and sweep to garbage collect old patches :)
	for(auto it = patches.begin(); it != patches.end(); ++it)
		it->second.in_new_set = false;

	// Add any new quad not in ground_quads.
	for(size_t z=0; z<NUM_NEW_QUADS; ++z)
	{
		const Vec2i new_quad = new_quads[z];
		auto res = patches.find(new_quad); // Look up new quad in map
		if(res != patches.end()) // If present:
			res->second.in_new_set = true; // Mark patch as being in new set
		else
		{
			Timer timer;

			// Make new patch
			Patch patch;

			const float z_range = 5.f; // Distance above and below camera to consider putting grass objects on

			// Get list of meshes intersecting this patch
			js::AABBox patch_consider_aabb = js::AABBox(
				Vec4f(new_quad.x *       patch_w, new_quad.y *       patch_w, -100, 1),
				Vec4f((new_quad.x + 1) * patch_w, (new_quad.y + 1) * patch_w, 400, 1)
			);
			physics_obs.resize(0);
			for(size_t q=0; q<park_biome_physics_objects.size(); ++q)
				if(patch_consider_aabb.intersectsAABB(park_biome_physics_objects[q]->getAABBoxWS()))
					physics_obs.push_back(park_biome_physics_objects[q].ptr());

			if(!physics_obs.empty())
			{
				// Scatter over extent of new patch
				const Vec3f base_scale(0.2f);
				const Vec3f scale_variation(0.0f);
			
				js::AABBox all_instances_aabb = js::AABBox::emptyAABBox();
				const js::AABBox instance_aabb_os = this->grass_ob->mesh_data->aabb_os;
				int num_scatter_points = 0;
				for(int i=0; i<N; ++i)
				{
					const float u = rng.unitRandom();
					const float v = rng.unitRandom();

					const Vec4f ray_trace_start_pos = Vec4f((new_quad.x + u) * patch_w, (new_quad.y + v) * patch_w, campos[2] + z_range, 1);
					const Vec4f trace_dir = Vec4f(0, 0,-1,0);
					// Trace ray down and get point on geometry
					for(size_t q=0; q<physics_obs.size(); ++q)
					{
						const PhysicsObject* const physics_ob = physics_obs[q];
						RayTraceResult result;
						physics_ob->traceRay(Ray(/*startpos=*/ray_trace_start_pos, /*unitdir=*/trace_dir, /*min_t=*/0, /*max_t=*/10000), result);
						if(result.hit_object != NULL)
						{
							const Vec4f hitpos_ws = ray_trace_start_pos + trace_dir * result.hitdist_ws;
							const float rot_z = Maths::get2Pi<float>() * rng.unitRandom();
							const Vec3f scale = base_scale + (rng.unitRandom() * rng.unitRandom() * rng.unitRandom()) * scale_variation;
							instance_matrices_temp[num_scatter_points] = instanceObToWorldMatrix(hitpos_ws, rot_z, scale) * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());

							all_instances_aabb.enlargeToHoldAABBox(instance_aabb_os.transformedAABBFast(instance_matrices_temp[num_scatter_points]));

							num_scatter_points++;
							break;
						}
					}
				}
			
				if(num_scatter_points > 0)
				{
					GLObjectRef gl_ob = opengl_engine.allocateObject();
					gl_ob->materials.resize(1);
					gl_ob->materials[0] = this->grass_ob->materials[0];
					gl_ob->materials[0].begin_fade_out_distance = patch_w * 1.5f;
					gl_ob->materials[0].end_fade_out_distance = patch_w * 2.f;

					gl_ob->ob_to_world_matrix.setToTranslationMatrix(new_quad.x * patch_w, new_quad.y * patch_w, 0);
					gl_ob->mesh_data = this->grass_ob->mesh_data;

					gl_ob->enableInstancing(*opengl_engine.vert_buf_allocator, instance_matrices_temp.data(), sizeof(Matrix4f) * num_scatter_points);

					opengl_engine.addObject(gl_ob);
					
					gl_ob->aabb_ws = all_instances_aabb; // Override AABB with an AABB that contains all instances.

					patch.opengl_obs.push_back(gl_ob);
				}

				// TEMP: add debug AABB object around patch
				//GLObjectRef debug_aabb = opengl_engine.makeAABBObject(patch_consider_aabb.min_, patch_consider_aabb.max_, Colour4f(1,patch_w / 150.f,0,0.5f));
				//opengl_engine.addObject(debug_aabb);
				//patch.opengl_obs.push_back(debug_aabb);
			} // end if(!physics_obs.empty())

			patches.insert(std::make_pair(new_quad, patch));

			// conPrint("Added patch (" + toString(new_quad.x) + ", " + toString(new_quad.y) + ") Elapsed: " + timer.elapsedStringNSigFigs(4));
		} // end if patch not already added
	} // end for each new patch

	// Remove any stale patches. (patches not in new set)
	for(auto it = patches.begin(); it != patches.end();)
	{
		Patch& patch = it->second;
		if(patch.in_new_set)
			++it;
		else
		{
			// Patch was not in new_quads, so remove it
			// conPrint("Removed ground quad (" + toString(it->first.x) + ", " + toString(it->first.y) + ")");
			
			// Remove opengl objects on the patch
			for(size_t t=0; t<patch.opengl_obs.size(); ++t)
				opengl_engine.removeObject(patch.opengl_obs[t]);

			it = patches.erase(it); // Erase patch from map, set iterator to next item in map
		}
	}
}


void BiomeManager::update(const Vec4f& campos, const Vec4f& cam_forwards_ws, const Vec4f& cam_right_ws, const Vec4f& sundir, OpenGLEngine& opengl_engine)
{
	try
	{
		if(grass_ob.isNull())
			return;

		// updatePatchSet(patches_a, 10.f, campos, cam_forwards_ws, cam_right_ws, sundir, opengl_engine);
		// updatePatchSet(patches_b, 30.f, campos, cam_forwards_ws, cam_right_ws, sundir, opengl_engine);
		// updatePatchSet(patches_c, 90.f, campos, cam_forwards_ws, cam_right_ws, sundir, opengl_engine);
	}
	catch(glare::Exception& e)
	{
		conPrint("BiomeManager::update() error: " + e.what());
	}
}
