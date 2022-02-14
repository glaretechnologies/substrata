/*=====================================================================
MeshBuilding.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshBuilding.h"


#include <opengl/OpenGLEngine.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <simpleraytracer/raymesh.h>
#include <utils/ShouldCancelCallback.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/StandardPrintOutput.h>


MeshBuilding::MeshBuildingResults MeshBuilding::makeImageCube(glare::TaskManager& task_manager)
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 1;

	// The y=0 and y=1 faces are the ones the image is actually applied to.

	// x=0 face
	unsigned int v_start = 0;
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(0,0,1));
		mesh->addVertex(Indigo::Vec3f(0,1,1));
		mesh->addVertex(Indigo::Vec3f(0,1,0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 1);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 1);
		v_start += 4;
	}
	// x=1 face
	{
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		mesh->addVertex(Indigo::Vec3f(1,1,0));
		mesh->addVertex(Indigo::Vec3f(1,1,1));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 1);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 1);
		v_start += 4;
	}
	// y=0 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		mesh->addVertex(Indigo::Vec3f(0,0,1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 0);
		v_start += 4;
	}
	// y=1 face
	{
		mesh->addVertex(Indigo::Vec3f(0,1,0));
		mesh->addVertex(Indigo::Vec3f(0,1,1));
		mesh->addVertex(Indigo::Vec3f(1,1,1));
		mesh->addVertex(Indigo::Vec3f(1,1,0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 0);
		v_start += 4;
	}
	// z=0 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(0,1,0));
		mesh->addVertex(Indigo::Vec3f(1,1,0));
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 1);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 1);
		v_start += 4;
	}
	// z=1 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,1));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		mesh->addVertex(Indigo::Vec3f(1,1,1));
		mesh->addVertex(Indigo::Vec3f(0,1,1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 0));
		mesh->uv_pairs.push_back(Indigo::Vec2f(1, 1));
		mesh->uv_pairs.push_back(Indigo::Vec2f(0, 1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, vertex_indices, 1);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, vertex_indices_2, 1);
		v_start += 4;
	}

	mesh->endOfModel();

	Reference<OpenGLMeshRenderData> image_cube_opengl_mesh = OpenGLEngine::buildIndigoMesh(mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	// Build RayMesh (for physics)
	Reference<RayMesh> image_cube_raymesh = new RayMesh("image_cube_mesh", /*enable shading normals=*/false);
	image_cube_raymesh->fromIndigoMesh(*mesh); // Use fromIndigoMesh instead of fromIndigoMeshForPhysics so we get UVs

	Geometry::BuildOptions options;
	DummyShouldCancelCallback should_cancel_callback;
	StandardPrintOutput print_output;
	image_cube_raymesh->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);


	MeshBuildingResults results;
	results.opengl_mesh_data = image_cube_opengl_mesh;
	results.raymesh = image_cube_raymesh;
	results.indigo_mesh = mesh;
	return results;
}


MeshBuilding::MeshBuildingResults MeshBuilding::makeSpotlightMeshes(glare::TaskManager& task_manager)
{
	const float fixture_w = 0.1;

	// Build Indigo::Mesh
	Indigo::MeshRef spotlight_mesh = new Indigo::Mesh();
	spotlight_mesh->num_uv_mappings = 1;

	spotlight_mesh->vert_positions.resize(4);
	spotlight_mesh->vert_normals.resize(4);
	spotlight_mesh->uv_pairs.resize(4);
	spotlight_mesh->quads.resize(1);

	spotlight_mesh->vert_positions[0] = Indigo::Vec3f(-fixture_w/2, -fixture_w/2, 0.f);
	spotlight_mesh->vert_positions[1] = Indigo::Vec3f(-fixture_w/2,  fixture_w/2, 0.f); // + y
	spotlight_mesh->vert_positions[2] = Indigo::Vec3f( fixture_w/2,  fixture_w/2, 0.f);
	spotlight_mesh->vert_positions[3] = Indigo::Vec3f( fixture_w/2, -fixture_w/2, 0.f); // + x

	spotlight_mesh->vert_normals[0] = Indigo::Vec3f(0, 0, -1);
	spotlight_mesh->vert_normals[1] = Indigo::Vec3f(0, 0, -1);
	spotlight_mesh->vert_normals[2] = Indigo::Vec3f(0, 0, -1);
	spotlight_mesh->vert_normals[3] = Indigo::Vec3f(0, 0, -1);

	spotlight_mesh->uv_pairs[0] = Indigo::Vec2f(0, 0);
	spotlight_mesh->uv_pairs[1] = Indigo::Vec2f(0, 1);
	spotlight_mesh->uv_pairs[2] = Indigo::Vec2f(1, 1);
	spotlight_mesh->uv_pairs[3] = Indigo::Vec2f(1, 0);

	spotlight_mesh->quads[0].mat_index = 0;
	spotlight_mesh->quads[0].vertex_indices[0] = 0;
	spotlight_mesh->quads[0].vertex_indices[1] = 1;
	spotlight_mesh->quads[0].vertex_indices[2] = 2;
	spotlight_mesh->quads[0].vertex_indices[3] = 3;
	spotlight_mesh->quads[0].uv_indices[0] = 0;
	spotlight_mesh->quads[0].uv_indices[1] = 1;
	spotlight_mesh->quads[0].uv_indices[2] = 2;
	spotlight_mesh->quads[0].uv_indices[3] = 3;

	spotlight_mesh->endOfModel();

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh = OpenGLEngine::buildIndigoMesh(spotlight_mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	// Build RayMesh (for physics)
	RayMeshRef spotlight_raymesh = new RayMesh("mesh", /*enable shading normals=*/false);
	spotlight_raymesh->fromIndigoMesh(*spotlight_mesh);

	spotlight_raymesh->buildTrisFromQuads();
	Geometry::BuildOptions options;
	DummyShouldCancelCallback should_cancel_callback;
	StandardPrintOutput print_output;
	spotlight_raymesh->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);

	MeshBuildingResults results;
	results.opengl_mesh_data = spotlight_opengl_mesh;
	results.raymesh = spotlight_raymesh;
	results.indigo_mesh = spotlight_mesh;
	return results;
}


Reference<RayMesh> MeshBuilding::makeUnitCubeRayMesh(glare::TaskManager& task_manager)
{
	RayMeshRef unit_cube_raymesh = new RayMesh("mesh", false);
	unit_cube_raymesh->addVertex(Vec3f(0, 0, 0));
	unit_cube_raymesh->addVertex(Vec3f(1, 0, 0));
	unit_cube_raymesh->addVertex(Vec3f(1, 1, 0));
	unit_cube_raymesh->addVertex(Vec3f(0, 1, 0));
	unit_cube_raymesh->addVertex(Vec3f(0, 0, 1));
	unit_cube_raymesh->addVertex(Vec3f(1, 0, 1));
	unit_cube_raymesh->addVertex(Vec3f(1, 1, 1));
	unit_cube_raymesh->addVertex(Vec3f(0, 1, 1));

	unsigned int uv_i[] ={ 0, 0, 0, 0 };
	{
		unsigned int v_i[] ={ 0, 3, 2, 1 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // z = 0 quad
	}
	{
		unsigned int v_i[] ={ 4, 5, 6, 7 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // z = 1 quad
	}
	{
		unsigned int v_i[] ={ 0, 1, 5, 4 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // y = 0 quad
	}
	{
		unsigned int v_i[] ={ 2, 3, 7, 6 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // y = 1 quad
	}
	{
		unsigned int v_i[] ={ 0, 4, 7, 3 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // x = 0 quad
	}
	{
		unsigned int v_i[] ={ 1, 2, 6, 5 };
		unit_cube_raymesh->addQuad(v_i, uv_i, 0); // x = 1 quad
	}

	unit_cube_raymesh->buildTrisFromQuads();
	Geometry::BuildOptions options;
	DummyShouldCancelCallback should_cancel_callback;
	StandardPrintOutput print_output;
	unit_cube_raymesh->build(options, should_cancel_callback, print_output, /*verbose=*/false, task_manager);

	return unit_cube_raymesh;
}
