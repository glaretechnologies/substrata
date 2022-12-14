/*=====================================================================
BuildScatteringInfoTask.cpp
---------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "BuildScatteringInfoTask.h"


#include "LoadTextureTask.h"
#include "ThreadMessages.h"
#include "ModelLoading.h"
#include "PhysicsObject.h"
#include "../shared/ResourceManager.h"
#include <indigo/TextureServer.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <simpleraytracer/raymesh.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <FileUtils.h>


BuildScatteringInfoTask::BuildScatteringInfoTask()
{}


BuildScatteringInfoTask::~BuildScatteringInfoTask()
{}


void BuildScatteringInfoTask::run(size_t thread_index)
{
	Reference<OpenGLMeshRenderData> gl_meshdata;
	PhysicsShape physics_shape;

	RayMeshRef raymesh = new RayMesh("scatter mesh", false);

	try
	{
		if(voxel_ob.nonNull())
		{
			const Matrix4f ob_to_world_matrix = obToWorldMatrix(*voxel_ob);

			if(voxel_ob->getCompressedVoxels().size() == 0)
			{
				return;
			}
			else
			{
				VoxelGroup voxel_group;
				WorldObject::decompressVoxelGroup(voxel_ob->getCompressedVoxels().data(), voxel_ob->getCompressedVoxels().size(), /*decompressed group out=*/voxel_group);

				js::Vector<bool, 16> mat_transparent(voxel_ob->materials.size());
				for(size_t i=0; i<voxel_ob->materials.size(); ++i)
					mat_transparent[i] = voxel_ob->materials[i]->opacity.val < 1.f;

				const bool need_lightmap_uvs = false;//!voxel_ob->lightmap_url.empty();
				Indigo::MeshRef indigo_mesh;
				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, /*subsample_factor=*/1, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
					need_lightmap_uvs, mat_transparent, /*physics shape out=*/physics_shape, /*indigo mesh out=*/indigo_mesh);

				raymesh->fromIndigoMesh(*indigo_mesh);
			}
		}
		else // Else not voxel ob, just loading a model:
		{
			assert(!lod_model_url.empty());

			BatchedMeshRef batched_mesh;

			// We want to load and build the mesh at lod_model_url.
			// conPrint("LoadModelTask: loading mesh with URL '" + lod_model_url + "'.");
			gl_meshdata = ModelLoading::makeGLMeshDataAndBatchedMeshForModelURL(lod_model_url, *this->resource_manager,
				/*vert_buf_allocator=*/NULL, 
				true, // skip_opengl_calls - we need to do these on the main thread.
				/*physics shape out=*/physics_shape,
				batched_mesh);

			raymesh->fromBatchedMesh(*batched_mesh);
		}

		std::vector<float> local_sub_elem_surface_areas;
		raymesh->getSubElementSurfaceAreas(
			this->ob_to_world,
			local_sub_elem_surface_areas
		);

		double A = 0;
		for(size_t i=0; i<local_sub_elem_surface_areas.size(); ++i)
			A += local_sub_elem_surface_areas[i];

		const float total_surface_area = (float)A;

		Reference<ObScatteringInfo> scattering_info = new ObScatteringInfo();
		scattering_info->aabb_ws = raymesh->getAABBox().transformedAABB(this->ob_to_world);
		scattering_info->raymesh = raymesh;
		scattering_info->total_surface_area = total_surface_area;
		scattering_info->uniform_dist.build(local_sub_elem_surface_areas); // Build DiscreteDistribution

		// Send a BuildScatteringInfoDoneThreadMessage back to main window.
		Reference<BuildScatteringInfoDoneThreadMessage> msg = new BuildScatteringInfoDoneThreadMessage();
		msg->ob_uid = this->ob_uid;
		msg->ob_scattering_info = scattering_info;
		result_msg_queue->enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Error while building scatter info: " + e.what()));
	}
}
