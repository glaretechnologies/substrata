/*=====================================================================
MeshBuilding.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshBuilding.h"


#include "PhysicsObject.h"
#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/GLMeshBuilding.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <simpleraytracer/raymesh.h>
#include <graphics/formatdecoderobj.h>
#include <graphics/FormatDecoderGLTF.h>
#include <utils/ShouldCancelCallback.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/StandardPrintOutput.h>
#include <tracy/Tracy.hpp>

#if USE_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#endif



MeshBuilding::MeshBuildingResults MeshBuilding::makeImageCube(VertexBufferAllocator& allocator)
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

	Reference<OpenGLMeshRenderData> image_cube_opengl_mesh = GLMeshBuilding::buildIndigoMesh(&allocator, mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = image_cube_opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	results.indigo_mesh = mesh;
	return results;
}


// Make a seat mesh with a backrest to show forward direction
// The seat is centered at the origin and extends from -0.5 to 0.5 in x and y
MeshBuilding::MeshBuildingResults MeshBuilding::makeSeatMesh(VertexBufferAllocator& allocator)
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 1;

	const float seat_half_size = 0.5f; // Half the width of the seat
	const float seat_height = 0.1f; // Height (thickness) of the seat cushion
	const float backrest_height = 0.5f; // Height of the backrest
	const float backrest_thickness = 0.05f; // Thickness of the backrest

	unsigned int v_start = 0;
	
	// Seat cushion (centered at origin)
	// x=-0.5 face (left)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, 0));
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
	// x=0.5 face (right)
	{
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height));
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
	// y=-0.5 face (back - where backrest attaches)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height));
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
	// y=0.5 face (front)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, 0));
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
	// z=0 face (bottom)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, 0));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, 0));
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
	// z=seat_height face (top - the seat surface)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, seat_half_size, seat_height));
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

	// Add backrest (vertical back at y = -0.5)
	// x=-0.5 face (left side of backrest)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size + backrest_thickness, seat_height));
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
	// x=0.5 face (right side of backrest)
	{
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size + backrest_thickness, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height + backrest_height));
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
	// y=-0.5 face (back face of backrest - what the sitter sees)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height + backrest_height));
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
	// y=-0.5 + backrest_thickness face (front face of backrest)
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size + backrest_thickness, seat_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size + backrest_thickness, seat_height));
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
	// Top of backrest
	{
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
		mesh->addVertex(Indigo::Vec3f(-seat_half_size, -seat_half_size + backrest_thickness, seat_height + backrest_height));
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

	mesh->endOfModel();

	Reference<OpenGLMeshRenderData> seat_opengl_mesh = GLMeshBuilding::buildIndigoMesh(&allocator, mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = seat_opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	results.indigo_mesh = mesh;
	return results;
}


MeshBuilding::MeshBuildingResults MeshBuilding::makeSpotlightMeshes(const std::string& base_dir_path, VertexBufferAllocator& allocator)
{
	ZoneScoped; // Tracy profiler

	const std::string model_path = base_dir_path + "/data/resources/spotlight5.bmesh";

	BatchedMeshRef batched_mesh = BatchedMesh::readFromFile(model_path, /*mem allocator=*/nullptr);

	batched_mesh->checkValidAndSanitiseMesh();

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh = GLMeshBuilding::buildBatchedMesh(&allocator, batched_mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = spotlight_opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*batched_mesh, /*is dynamic=*/false);
	return results;
}


MeshBuilding::MeshBuildingResults MeshBuilding::makePortalMeshes(const std::string& base_dir_path, VertexBufferAllocator& allocator)
{
	ZoneScoped; // Tracy profiler

	const std::string model_path = base_dir_path + "/data/resources/portal.bmesh";

	BatchedMeshRef batched_mesh = BatchedMesh::readFromFile(model_path, /*mem allocator=*/nullptr);

	batched_mesh->checkValidAndSanitiseMesh();

	Reference<OpenGLMeshRenderData> portal_opengl_mesh = GLMeshBuilding::buildBatchedMesh(&allocator, batched_mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	js::Vector<bool> create_tris_for_mat(4, true);
	create_tris_for_mat[3] = false; // Material with index 3 is the blue portal shader material that shouldn't be collidable.
	PhysicsShape arch_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*batched_mesh, /*build_dynamic_physics_ob=*/false, /*mem allocator=*/nullptr, &create_tris_for_mat);


	JPH::Ref<JPH::StaticCompoundShapeSettings> compound_settings = new JPH::StaticCompoundShapeSettings();
	compound_settings->AddShape(JPH::Vec3Arg(0,0,0), JPH::QuatArg::sIdentity(), arch_shape.jolt_shape, /*inUserData=*/0);

	JPH::Ref<JPH::BoxShapeSettings> box_settings = new JPH::BoxShapeSettings(/*inHalfExtent=*/JPH::Vec3Arg(0.5f, 0.06f, 1.f));

	compound_settings->AddShape(/*position=*/JPH::Vec3Arg(0,0,1.f), JPH::QuatArg::sIdentity(), box_settings, /*inUserData=*/1);


	JPH::Result<JPH::Ref<JPH::Shape>> result = compound_settings->Create();
	if(result.HasError())
		throw glare::Exception(std::string("Error building Jolt shape: ") + result.GetError().c_str());
	JPH::Ref<JPH::Shape> compound_shape = result.Get();


	MeshBuildingResults results;
	results.opengl_mesh_data = portal_opengl_mesh;
	results.physics_shape.jolt_shape = compound_shape;
	results.physics_shape.size_B = PhysicsWorld::computeSizeBForShape(compound_shape);
	return results;
}


Reference<Indigo::Mesh> MeshBuilding::makeUnitCubeIndigoMesh()
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 0;

	const unsigned int uv_indices[]   = {0, 0, 0};

	// x=0 face
	unsigned int v_start = 0;
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(0,0,1));
		mesh->addVertex(Indigo::Vec3f(0,1,1));
		mesh->addVertex(Indigo::Vec3f(0,1,0));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, uv_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, uv_indices, 0);
		v_start += 4;
	}
	// x=1 face
	{
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		mesh->addVertex(Indigo::Vec3f(1,1,0));
		mesh->addVertex(Indigo::Vec3f(1,1,1));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, uv_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, uv_indices, 0);
		v_start += 4;
	}
	// y=0 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		mesh->addVertex(Indigo::Vec3f(0,0,1));
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
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, uv_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, uv_indices, 0);
		v_start += 4;
	}
	// z=0 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,0));
		mesh->addVertex(Indigo::Vec3f(0,1,0));
		mesh->addVertex(Indigo::Vec3f(1,1,0));
		mesh->addVertex(Indigo::Vec3f(1,0,0));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, uv_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, uv_indices, 0);
		v_start += 4;
	}
	// z=1 face
	{
		mesh->addVertex(Indigo::Vec3f(0,0,1));
		mesh->addVertex(Indigo::Vec3f(1,0,1));
		mesh->addVertex(Indigo::Vec3f(1,1,1));
		mesh->addVertex(Indigo::Vec3f(0,1,1));
		const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
		mesh->addTriangle(vertex_indices, uv_indices, 0);
		const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
		mesh->addTriangle(vertex_indices_2, uv_indices, 0);
		v_start += 4;
	}

	mesh->endOfModel();

	return mesh;
}


PhysicsShape MeshBuilding::makeUnitCubePhysicsShape(VertexBufferAllocator& allocator)
{
	Indigo::MeshRef mesh = makeUnitCubeIndigoMesh();

	PhysicsShape physics_shape;
	physics_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	return physics_shape;
}
