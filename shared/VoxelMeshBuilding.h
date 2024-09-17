/*=====================================================================
VoxelMeshBuilding.h
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <dll/include/IndigoMesh.h>
#include <maths/vec3.h>
#include <utils/Vector.h>
class VoxelGroup;
namespace glare { class Allocator; }


/*=====================================================================
VoxelMeshBuilding
-----------------
This code is all the stuff for voxel meshing that doesn't involve OpenGL,
so it can be used in LightMapperBot as well as gui_client.
=====================================================================*/
class VoxelMeshBuilding
{
public:
	// If mats_transparent is lacking entries for a particular material index, the material is assumed to be opaque.
	static Reference<Indigo::Mesh> makeIndigoMeshForVoxelGroup(const VoxelGroup& voxel_group, const int subsample_factor, const js::Vector<bool, 16>& mats_transparent,
		glare::Allocator* mem_allocator);


	// Build a mesh with shading normals and UVs.  This is used in ChunkGenThread.
	static Reference<Indigo::Mesh> makeIndigoMeshWithShadingNormalsForVoxelGroup(const VoxelGroup& voxel_group, const int subsample_factor, const js::Vector<bool, 16>& mats_transparent,
		glare::Allocator* mem_allocator);

	static void test();
};
