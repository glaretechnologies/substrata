/*=====================================================================
MeshBuilding.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include <utils/Reference.h>
#include <string>


class OpenGLMeshRenderData;
class VertexBufferAllocator;
class RayMesh;
class PhysicsShape;
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
		PhysicsShape physics_shape;
		Reference<Indigo::Mesh> indigo_mesh;
	};

	// Make a cube with the first material on the front and back faces (-y and +y directions), and the second material on the side faces.
	// Loads mesh data into current OpenGL context.
	static MeshBuildingResults makeImageCube(VertexBufferAllocator& allocator);

	static MeshBuildingResults makeSpotlightMeshes(const std::string& base_dir_path, VertexBufferAllocator& allocator);

	static Reference<Indigo::Mesh> makeUnitCubeIndigoMesh();

	static PhysicsShape makeUnitCubePhysicsShape(VertexBufferAllocator& allocator);

	static Reference<OpenGLMeshRenderData> makeRotationArcHandleMeshData(VertexBufferAllocator& allocator, float arc_end_angle);
};
