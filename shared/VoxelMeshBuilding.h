/*=====================================================================
VoxelMeshBuilding.h
-------------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#pragma once


//#include "../shared/WorldMaterial.h"
//#include "../shared/WorldObject.h"
//#include <opengl/OpenGLEngine.h>
//#include <dll/include/IndigoMesh.h>
#include <graphics/BatchedMesh.h>

//struct GLObject;
//class Matrix4f;
//class ResourceManager;
//class RayMesh;
class VoxelGroup;
namespace Indigo { class TaskManager; }


/*=====================================================================
VoxelMeshBuilding
-----------------
NOTE: some duplicated code from ModelLoading!
=====================================================================*/
class VoxelMeshBuilding
{
public:
	//static Reference<OpenGLMeshRenderData> makeModelForVoxelGroup(const VoxelGroup& voxel_group, Indigo::TaskManager& task_manager, bool do_opengl_stuff, Reference<RayMesh>& raymesh_out);

	static Reference<BatchedMesh> makeBatchedMeshForVoxelGroup(const VoxelGroup& voxel_group);

	static void test();
};
