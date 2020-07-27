/*=====================================================================
LoadModelTask.cpp
-----------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "LoadModelTask.h"


#include "MainWindow.h"
#include "LoadTextureTask.h"
#include <indigo/TextureServer.h>
#include <graphics/imformatdecoder.h>
#include <opengl/OpenGLEngine.h>
#include <ConPrint.h>
#include <PlatformUtils.h>


LoadModelTask::LoadModelTask()
{}


LoadModelTask::~LoadModelTask()
{}


void LoadModelTask::run(size_t thread_index)
{
	const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

	GLObjectRef opengl_ob;
	PhysicsObjectRef physics_ob;

	try
	{
		if(ob->object_type == WorldObject::ObjectType_Hypercard)
		{
			return; // Done in main thread for now.
		}
		else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
		{
			if(ob->voxel_group.voxels.size() == 0)
			{
				// Add dummy cube marker for zero-voxel case.
				physics_ob = new PhysicsObject(/*collidable=*/false);
				physics_ob->geometry = main_window->unit_cube_raymesh;
				physics_ob->ob_to_world = ob_to_world_matrix * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);

				opengl_ob = new GLObject();
				opengl_ob->mesh_data = opengl_engine->getCubeMeshData();
				opengl_ob->materials.resize(1);
				opengl_ob->materials[0].albedo_rgb = Colour3f(0.8);
//				opengl_ob->materials[0].albedo_tex_path = "resources/voxel_dummy_texture.png";
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
			}
			else
			{
				Reference<RayMesh> raymesh;
				Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(ob->voxel_group, *model_building_task_manager, /*do_opengl_stuff=*/false, raymesh);

				physics_ob = new PhysicsObject(/*collidable=*/ob->isCollidable());
				physics_ob->geometry = raymesh;
				physics_ob->ob_to_world = ob_to_world_matrix;

				opengl_ob = new GLObject();
				opengl_ob->mesh_data = gl_meshdata;
				opengl_ob->materials.resize(ob->materials.size());
				for(uint32 i=0; i<ob->materials.size(); ++i)
					ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob->lightmap_url, *this->resource_manager, opengl_ob->materials[i]);
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;
			}
		}
		else
		{
			assert(ob->object_type == WorldObject::ObjectType_Generic);

			// We want to load and build the mesh at ob->model_url.
			const bool just_inserted = main_window->checkAddModelToProcessedSet(ob->model_url); // Mark model as being processed so another LoadModelTask doesn't try and process it also.
			if(just_inserted)
			{
				// conPrint("LoadModelTask: loading mesh with URL '" + ob->model_url + "'.");
				Reference<RayMesh> raymesh;
				opengl_ob = ModelLoading::makeGLObjectForModelURLAndMaterials(ob->model_url, ob->materials, ob->lightmap_url, *this->resource_manager, *this->mesh_manager, *model_building_task_manager, ob_to_world_matrix,
					true, // skip_opengl_calls - we need to do these on the main thread.
					raymesh);

				// Make physics object
				physics_ob = new PhysicsObject(/*collidable=*/ob->isCollidable());
				physics_ob->geometry = raymesh;
				physics_ob->ob_to_world = ob_to_world_matrix;
			}
		}

		if(physics_ob.nonNull())
		{
			physics_ob->userdata = ob.ptr();
			physics_ob->userdata_type = 0;
		}

		if(opengl_ob.nonNull()) // If we actually loaded a model:
		{
			// Send a ModelLoadedThreadMessage back to main window.
			Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
			msg->opengl_ob = opengl_ob;
			msg->physics_ob = physics_ob;
			msg->ob = ob;
			main_window->msg_queue.enqueue(msg);
		}
	}
	catch(Indigo::Exception& e)
	{
		conPrint(e.what());
	}
}
