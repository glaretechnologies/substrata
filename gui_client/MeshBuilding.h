/*=====================================================================
MeshBuilding.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/Reference.h>


class OpenGLMeshRenderData;
class VertexBufferAllocator;
class RayMesh;
namespace glare { class TaskManager; }
namespace Indigo { class Mesh; }




/*=====================================================================
MeshBuilding
------------
Building of various reused meshes.
=====================================================================*/
class MeshBuilding
{
public:

	struct MeshBuildingResults
	{
		Reference<OpenGLMeshRenderData> opengl_mesh_data;
		Reference<RayMesh> raymesh;
		Reference<Indigo::Mesh> indigo_mesh;
	};

	// Make a cube with the first material on the front and back faces (-y and +y directions), and the second material on the side faces.
	// Loads mesh data into current OpenGL context.
	static MeshBuildingResults makeImageCube(glare::TaskManager& task_manager, VertexBufferAllocator& allocator);

	static MeshBuildingResults makeSpotlightMeshes(glare::TaskManager& task_manager, VertexBufferAllocator& allocator);

	static Reference<RayMesh> makeUnitCubeRayMesh(glare::TaskManager& task_manager, VertexBufferAllocator& allocator);
};
