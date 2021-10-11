/*=====================================================================
BiomeManager.cpp
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "BiomeManager.h"


#include "WorldState.h"
#include "PhysicsObject.h"
#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include "simpleraytracer/raymesh.h"
#include "physics/HashedGrid.h"
#include "maths/PCG32.h"
#include <Exception.h>


BiomeManager::BiomeManager()
{
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
	ResourceManager& resource_manager, GLObjectRef prototype_ob, RayMeshRef raymesh, float density, float evenness, bool add_physics_objects, const Vec3f& base_scale, const Vec3f& scale_variation, float z_offset,
	js::Vector<BiomeObInstance, 16>& instances, js::AABBox& instances_aabb_ws_in_out)
{
	world_ob.physics_object->buildUniformSampler(); // Check built.

	const float surface_area = world_ob.physics_object->total_surface_area * 0.5f; // NOTE: assuming cube, ignoring underfaces.

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

	HashedGrid<Vec3f> hashed_grid(world_ob.physics_object->aabb_ws, cell_w,
		N // Expected num items
	);

	// Compute AABB over all instances of the object
	js::AABBox all_instances_aabb_ws = js::AABBox::emptyAABBox();

	//int instance_i = 0;
	const size_t initial_num_instances = instances.size();
	for(int i=0; i<N; ++i)
	{
		const float u1 = rng.unitRandom();
		const float u = rng.unitRandom();
		const float v = rng.unitRandom();

		PhysicsObject::SampleSurfaceResults results;
		world_ob.physics_object->sampleSurfaceUniformly(u1, Vec2f(u, v), results);

		const Vec4f scatterpos = results.pos;
		const Vec3f scatterpos_vec3(results.pos);

		bool scatterpos_valid = true;
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
		const Vec4f pos = results.pos + Vec4f(0, 0, z_offset, 0);
		const float rot_z = Maths::get2Pi<float>() * rng.unitRandom();
		const Vec3f scale = base_scale + (rng.unitRandom() * rng.unitRandom() * rng.unitRandom()) * scale_variation;
		instance.to_world = instanceObToWorldMatrix(pos, rot_z, scale);
		instance.to_world_no_rot = instanceObToWorldMatrix(pos, 0.f, scale);

		instances.push_back(instance);

		// Make physics object
		if(add_physics_objects)
		{
			PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
			physics_ob->geometry = raymesh;
			physics_ob->ob_to_world = instance.to_world;

			physics_ob->userdata = NULL;
			physics_ob->userdata_type = 0;

			physics_world.addObject(physics_ob);
		}

		all_instances_aabb_ws.enlargeToHoldAABBox(prototype_ob->mesh_data->aabb_os.transformedAABBFast(instance.to_world));
	}

	const size_t num_instances_added = instances.size() - initial_num_instances;

	// conPrint("Added " + toString(num_instances_added) + " out of " + toString(N) + " candidate points");

	instances_aabb_ws_in_out.enlargeToHoldAABBox(all_instances_aabb_ws);
}


static GLObjectRef makeElmTreeOb(MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager, RayMeshRef& raymesh_out)
{
	std::vector<WorldMaterialRef> materials(2);
	materials[0] = new WorldMaterial();
	materials[0]->colour_rgb = Colour3f(0.7f);
	materials[0]->colour_texture_url = "GLB_image_11255090336016867094_jpg_11255090336016867094.jpg";
	materials[0]->roughness.val = 0.f;
	materials[1] = new WorldMaterial();
	materials[1]->colour_rgb = Colour3f(1.f); // TEMP different
	materials[1]->colour_texture_url = "elm_leaf_new_png_17162787394814938526.png";

	GLObjectRef tree_opengl_ob = ModelLoading::makeGLObjectForModelURLAndMaterials("elm_RT_glb_3393252396927074015.bmesh", /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, mesh_manager, task_manager, 
		/*ob to world matrix=*/Matrix4f::identity(), /*skip opengl calls=*/false, raymesh_out);

	for(size_t i=0; i<tree_opengl_ob->materials.size(); ++i)
		tree_opengl_ob->materials[i].imposterable = true; // Mark mats as imposterable so they can smoothly blend out


	if(tree_opengl_ob->mesh_data->vert_vbo.isNull()) // If this data has not been loaded into OpenGL yet:
		OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(*tree_opengl_ob->mesh_data); // Load mesh data into OpenGL

	tree_opengl_ob->is_instanced_ob_with_imposters = true;

	return tree_opengl_ob;
}


static GLObjectRef makeElmTreeImposterOb(MeshManager& mesh_manager, glare::TaskManager& task_manager, ResourceManager& resource_manager, OpenGLTextureRef back_lit_tex)
{
	std::vector<WorldMaterialRef> materials(1);
	materials[0] = new WorldMaterial();
	materials[0]->colour_rgb = Colour3f(1.f);
	materials[0]->colour_texture_url = "left_lit_png_3976582748380884323.png";
	materials[0]->roughness.val = 0.f;
	materials[0]->flags = WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;

	RayMeshRef raymesh;
	GLObjectRef tree_imposter_opengl_ob = ModelLoading::makeGLObjectForModelURLAndMaterials("Quad_obj_17249492137259942610.bmesh", /*ob lod level=*/0, materials, /*lightmap URL=*/"", resource_manager, mesh_manager, task_manager, 
		/*ob to world matrix=*/Matrix4f::identity(), /*skip opengl calls=*/false, raymesh);

	for(size_t i=0; i<tree_imposter_opengl_ob->materials.size(); ++i)
		tree_imposter_opengl_ob->materials[i].imposter = true; // Mark mats as imposters so they use the imposter shader

	if(tree_imposter_opengl_ob->mesh_data->vert_vbo.isNull()) // If this data has not been loaded into OpenGL yet:
		OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(*tree_imposter_opengl_ob->mesh_data); // Load mesh data into OpenGL

	tree_imposter_opengl_ob->materials[0].albedo_texture = back_lit_tex;
	tree_imposter_opengl_ob->is_imposter = true;
	tree_imposter_opengl_ob->is_instanced_ob_with_imposters = true;

	return tree_imposter_opengl_ob;
}


void BiomeManager::addObjectToBiome(WorldObject& world_ob, WorldState& world_state, PhysicsWorld& physics_world, MeshManager& mesh_manager, glare::TaskManager& task_manager, OpenGLEngine& opengl_engine,
	ResourceManager& resource_manager)
{
	if(world_ob.physics_object.isNull())
		return;

	if(world_ob.content == "biome: park")
	{
		world_ob.physics_object->buildUniformSampler(); // Check built.

		RayMeshRef tree_raymesh;
		GLObjectRef tree_opengl_ob = makeElmTreeOb(mesh_manager, task_manager, resource_manager, tree_raymesh);

		// Compute some tree instance points
		// Scatter elm tree
		js::Vector<BiomeObInstance, 16> ob_instances;
		js::AABBox ob_trees_aabb_ws;
		addScatteredObjects(world_ob, world_state, physics_world, mesh_manager, task_manager, opengl_engine, resource_manager, tree_opengl_ob, tree_raymesh,
			0.005f, // density
			0.6f, // evenness
			true, // add_physics_objects
			Vec3f(3.f), // base scale
			Vec3f(1.f), // scale variation
			0.f, // z offset
			ob_instances, // tree instances in/out
			ob_trees_aabb_ws
		);

		tree_opengl_ob->instance_info.resize(ob_instances.size());//tree_opengl_ob->instance_info.size() + tree_instances.size());

		js::Vector<Matrix4f, 16> instance_matrices(ob_instances.size());
		for(size_t z=0; z<ob_instances.size(); ++z)
		{
			instance_matrices[z] = ob_instances[z].to_world;

			tree_opengl_ob->instance_info[z].to_world = ob_instances[z].to_world;
		}

		tree_opengl_ob->enableInstancing(new VBO(instance_matrices.data(), sizeof(Matrix4f) * instance_matrices.size()));
		
		opengl_engine.addObject(tree_opengl_ob);
		tree_opengl_ob->aabb_ws = ob_trees_aabb_ws; // override AABB with AABB of all instances



		// Add the imposter instances to the opengl engine as well
		GLObjectRef tree_imposter_opengl_ob = makeElmTreeImposterOb(mesh_manager, task_manager, resource_manager, elm_imposters_tex);

		tree_imposter_opengl_ob->instance_info.resize(ob_instances.size());
		js::Vector<Matrix4f, 16> imposter_matrices(ob_instances.size());
		for(size_t z=0; z<ob_instances.size(); ++z)
		{
			// Elm imposters are approx 0.64 times as wide as high
			const float master_scale = 4.5;
			imposter_matrices[z] = Matrix4f::translationMatrix(0,0,master_scale * 1.3f) * ob_instances[z].to_world_no_rot * Matrix4f::scaleMatrix(master_scale, master_scale * 0.64, master_scale); //Matrix4f::translationMatrix(tree_instances[z].pos + Vec4f(0,0,5.5,0)) * Matrix4f::uniformScaleMatrix(14);// * Matrix4f::rotationAroundZAxis(-Maths::pi_2<float>());

			tree_imposter_opengl_ob->instance_info[z].to_world = imposter_matrices[z];
		}
		tree_imposter_opengl_ob->enableInstancing(new VBO(imposter_matrices.data(), sizeof(Matrix4f) * imposter_matrices.size()));

		opengl_engine.addObject(tree_imposter_opengl_ob);
		tree_imposter_opengl_ob->aabb_ws = ob_trees_aabb_ws; // override AABB with AABB of all instances
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

