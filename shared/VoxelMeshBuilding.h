/*=====================================================================
VoxelMeshBuilding.h
-------------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#pragma once


#include <dll/include/IndigoMesh.h>

class VoxelGroup;
//namespace Indigo { class TaskManager; }


/*=====================================================================
VoxelMeshBuilding
-----------------
This code is all the stuff for voxel meshing that doesn't involve OpenGL,
so it can be used in LightMapperBot as well as gui_client.
=====================================================================*/
class VoxelMeshBuilding
{
public:
	static Reference<Indigo::Mesh> makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group);

	static void test();
};
