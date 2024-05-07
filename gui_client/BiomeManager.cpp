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


BiomeManager::BiomeManager()
{
}


void BiomeManager::clear(OpenGLEngine& opengl_engine, PhysicsWorld& physics_world)
{
}


void BiomeManager::initTexturesAndModels(const std::string& resources_dir_path, OpenGLEngine& opengl_engine, ResourceManager& resource_manager)
{
	if(!resource_manager.isFileForURLPresent("Quad_obj_17249492137259942610.bmesh"))
		resource_manager.copyLocalFileToResourceDir(resources_dir_path + "/Quad_obj_17249492137259942610.bmesh", "Quad_obj_17249492137259942610.bmesh");
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

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(model_path,
			opengl_engine.vert_buf_allocator.ptr(), /*skip opengl calls=*/false, /*build_dynamic_physics_ob=*/false, opengl_engine.mem_allocator.ptr(), physics_shape);

		elm_tree_physics_shape = physics_shape;
		elm_tree_mesh_render_data = gl_meshdata;
	}

	// Build Elm tree materials
	if(elm_tree_gl_materials.empty())
	{
		elm_tree_gl_materials.resize(2);

		// Material 0 - bark
		elm_tree_gl_materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(162/256.f));
		elm_tree_gl_materials[0].albedo_texture = elm_bark_tex;
		elm_tree_gl_materials[0].roughness = 1;
		elm_tree_gl_materials[0].imposterable = true; // Mark mats as imposterable so they can smoothly blend out
		elm_tree_gl_materials[0].use_wind_vert_shader = true;

		// Material 1 - leaves
		elm_tree_gl_materials[1].albedo_linear_rgb = toLinearSRGB(Colour3f(162/256.f));
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
