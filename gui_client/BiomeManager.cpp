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
#include "graphics/SRGBUtils.h"
#include "maths/PCG32.h"
#include <Exception.h>
#include <RuntimeCheck.h>
#include <Parser.h>
#include <MemMappedFile.h>


BiomeManager::BiomeManager()
{
}


void BiomeManager::clear(OpenGLEngine& opengl_engine, PhysicsWorld& physics_world)
{
}


void BiomeManager::initTexturesAndModels(const std::string& resources_dir_path, OpenGLEngine& opengl_engine, ResourceManager& resource_manager)
{
	const URLString quad_mesh_URL = "Quad_obj_17249492137259942610.bmesh";
	if(resource_manager.getExistingResourceForURL(quad_mesh_URL).isNull())
	{
		ResourceRef resource = new Resource(quad_mesh_URL, /*local (abs) path=*/resources_dir_path + "/" + toStdString(quad_mesh_URL), Resource::State_Present, UserID(), /*external_resource=*/true);
		resource_manager.addResource(resource);
	}

//	if(!resource_manager.isFileForURLPresent("grass_2819211535648845788.bmesh"))
//		resource_manager.copyLocalFileToResourceDir(base_dir_path + "/resources/grass_2819211535648845788.bmesh", "grass_2819211535648845788.bmesh");

	if(elm_imposters_tex.isNull())
		elm_imposters_tex = opengl_engine.getTexture(resources_dir_path + "/imposters/elm_imposters.png");
	
	if(elm_bark_tex.isNull())
		elm_bark_tex = opengl_engine.getTexture(resources_dir_path + "/elm_bark_11255090336016867094.jpg");
	
	if(elm_leaf_tex.isNull())
		elm_leaf_tex = opengl_engine.getTexture(resources_dir_path + "/elm_leaf_frontface.png");
	
	if(elm_leaf_backface_tex.isNull())
		elm_leaf_backface_tex = opengl_engine.getTexture(resources_dir_path + "/elm_leaf_backface.png");

	if(elm_leaf_transmission_tex.isNull())
		elm_leaf_transmission_tex = opengl_engine.getTexture(resources_dir_path + "/elm_leaf_transmission.png");
	
	//if(elm_leaf_normal_map.isNull())
	//	elm_leaf_normal_map = opengl_engine.getTexture(resources_dir_path + "/resources/elm_leaf_normals.png");


	// Build elm tree opengl and physics geometry.
	if(elm_tree_mesh_render_data.isNull())
	{
		const std::string model_path = resources_dir_path + "/elm_RT_glb_3393252396927074015.bmesh";

		MemMappedFile file(model_path);
		ArrayRef<uint8> model_buffer((const uint8*)file.fileData(), file.fileSize());

		PhysicsShape physics_shape;

		js::Vector<bool> create_tris_for_mat(2);
		create_tris_for_mat[0] = true;
		create_tris_for_mat[1] = false; // Don't create physics triangles for leaves.

		Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(model_path, model_buffer,
			opengl_engine.vert_buf_allocator.ptr(), /*skip opengl calls=*/false, /*build_physics_ob=*/true, /*build_dynamic_physics_ob=*/false, create_tris_for_mat, 
			opengl_engine.mem_allocator.ptr(), physics_shape);

		elm_tree_physics_shape = physics_shape;
		elm_tree_mesh_render_data = gl_meshdata;
	}

	// Build Elm tree materials
	if(elm_tree_gl_materials.empty())
	{
		elm_tree_gl_materials.resize(2);

		// Material 0 - bark
		elm_tree_gl_materials[0].albedo_linear_rgb = Colour3f(0.6f);
		elm_tree_gl_materials[0].albedo_texture = elm_bark_tex;
		elm_tree_gl_materials[0].roughness = 1;
		elm_tree_gl_materials[0].imposterable = true; // Mark mats as imposterable so they can smoothly blend out
		elm_tree_gl_materials[0].use_wind_vert_shader = true;

		// Material 1 - leaves
		elm_tree_gl_materials[1].albedo_linear_rgb = Colour3f(0.35f); // Leaf reflection texture colour is too bright, reduce values.
		elm_tree_gl_materials[1].transmission_albedo_linear_rgb = Colour3f(1.1f); // Leaf transmission colour is possibly slightly too weak, increase values.
		elm_tree_gl_materials[1].roughness = 0.5f;
		elm_tree_gl_materials[1].fresnel_scale = 0.3f;
		elm_tree_gl_materials[1].imposterable = true; // Mark mats as imposterable so they can smoothly blend out
		elm_tree_gl_materials[1].use_wind_vert_shader = true;
		elm_tree_gl_materials[1].fancy_double_sided = true;
		elm_tree_gl_materials[1].albedo_texture = elm_leaf_tex;
		elm_tree_gl_materials[1].backface_albedo_texture = elm_leaf_backface_tex;
		elm_tree_gl_materials[1].transmission_texture = elm_leaf_transmission_tex;
		//elm_tree_gl_materials[1].normal_map = elm_leaf_normal_map;
	}
}
