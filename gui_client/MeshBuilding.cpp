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
#include <opengl/VertexBufferAllocator.h>
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


static inline Indigo::Vec2f toIndigoVec2f(const Vec2f& v)
{
	return Indigo::Vec2f(v.x, v.y);
}


static inline Indigo::Vec3f toIndigoVec3f(const Vec3f& v)
{
	return Indigo::Vec3f(v.x, v.y, v.z);
}


MeshBuilding::MeshBuildingResults MeshBuilding::makeConeMesh(const std::string& base_dir_path, VertexBufferAllocator& allocator)
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 1;

	const int res = 40;

	Indigo::Vector<Indigo::Vec3f> verts;
	Indigo::Vector<Indigo::Vec3f> normals;
	Indigo::Vector<Indigo::Vec2f> uvs;
	Indigo::Vector<uint32> indices;

	const Vec3f up(0,0,1);
	const Vec3f tip = up;
	const float base_r = 0.5f;

	// Define arrow head
	const int segments = 8;
	for(int s=0; s<segments; ++s)
	{
		float bot_vert_frac = (float)s       / segments;
		float top_vert_frac = (float)(s + 1) / segments;
		float bot_r = base_r - base_r * bot_vert_frac;
		float top_r = base_r - base_r * top_vert_frac;

		for(int i=0; i<res; ++i)
		{
			const float angle      = i       * Maths::get2Pi<float>() / res;
			const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

			const Vec3f unit_radius_a(cos(angle),      sin(angle),      0); // vector from axis of cone to edge of base at angle.  for v0 and v3
			const Vec3f unit_radius_b(cos(next_angle), sin(next_angle), 0); // vector from axis of cone to edge of base at next_angle.  for v1 and v2

			/*
			v3    v2
			
			v0    v1
			*/

			const Vec3f v0 = unit_radius_a * bot_r + up * bot_vert_frac;
			const Vec3f v1 = unit_radius_b * bot_r + up * bot_vert_frac;
			const Vec3f v2 = unit_radius_b * top_r + up * top_vert_frac;
			const Vec3f v3 = unit_radius_a * top_r + up * top_vert_frac;

			// Get radius vector, make orthogonal to vector from vertex at base to tip.
			const Vec3f normal_a = normalise(removeComponentInDir(unit_radius_a, normalise(tip - unit_radius_a * base_r)));
			const Vec3f normal_b = normalise(removeComponentInDir(unit_radius_b, normalise(tip - unit_radius_b * base_r)));

			normals.push_back(toIndigoVec3f(normal_a));
			normals.push_back(toIndigoVec3f(normal_b));
			normals.push_back(toIndigoVec3f(normal_b));
			normals.push_back(toIndigoVec3f(normal_a));

			verts.push_back(toIndigoVec3f(v0));
			verts.push_back(toIndigoVec3f(v1));
			verts.push_back(toIndigoVec3f(v2));
			verts.push_back(toIndigoVec3f(v3));

			uvs.push_back(toIndigoVec2f(Vec2f(angle      / Maths::get2Pi<float>(), bot_vert_frac)));
			uvs.push_back(toIndigoVec2f(Vec2f(next_angle / Maths::get2Pi<float>(), bot_vert_frac)));
			uvs.push_back(toIndigoVec2f(Vec2f(next_angle / Maths::get2Pi<float>(), top_vert_frac)));
			uvs.push_back(toIndigoVec2f(Vec2f(angle      / Maths::get2Pi<float>(), top_vert_frac)));


			indices.push_back((uint32)verts.size() - 4); 
			indices.push_back((uint32)verts.size() - 3); 
			indices.push_back((uint32)verts.size() - 2); 
			indices.push_back((uint32)verts.size() - 1);
		}
	}

	
	// Draw disc on bottom of cone
	for(int i=0; i<res; ++i)
	{
		const float angle      = i       * Maths::get2Pi<float>() / res;
		const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

		const Vec3f unit_radius_a(cos(angle),      sin(angle),      0); // vector from axis of cone to edge of base at angle.  for v0 and v3
		const Vec3f unit_radius_b(cos(next_angle), sin(next_angle), 0); // vector from axis of cone to edge of base at next_angle.  for v1 and v2

		/*
		v0 and v1 lie on cone axis at (0,0,0).
		Use 4 vertices since we are using quads for this mesh.

		v3    v2
			
		v0    v1
		*/

		const Vec3f v0 = Vec3f(0.f);
		const Vec3f v1 = Vec3f(0.f);
		const Vec3f v2 = unit_radius_b * base_r;
		const Vec3f v3 = unit_radius_a * base_r;

		const Vec3f normal = -up;

		normals.push_back(toIndigoVec3f(normal));
		normals.push_back(toIndigoVec3f(normal));
		normals.push_back(toIndigoVec3f(normal));
		normals.push_back(toIndigoVec3f(normal));

		verts.push_back(toIndigoVec3f(v0));
		verts.push_back(toIndigoVec3f(v1));
		verts.push_back(toIndigoVec3f(v2));
		verts.push_back(toIndigoVec3f(v3));

		uvs.push_back(toIndigoVec2f(Vec2f(0.5f, 0.5f)));
		uvs.push_back(toIndigoVec2f(Vec2f(0.5f, 0.5f)));
		uvs.push_back(toIndigoVec2f(Vec2f(1.f - (v2.x + 0.5f), v2.y + 0.5f)));
		uvs.push_back(toIndigoVec2f(Vec2f(1.f - (v3.x + 0.5f), v3.y + 0.5f)));

		indices.push_back((uint32)verts.size() - 4); 
		indices.push_back((uint32)verts.size() - 3); 
		indices.push_back((uint32)verts.size() - 2); 
		indices.push_back((uint32)verts.size() - 1);
	}



	mesh->vert_positions = verts;
	mesh->vert_normals = normals;
	mesh->uv_pairs = uvs;
	mesh->quads.resize(indices.size() / 4);
	for(size_t q=0; q<indices.size() / 4; ++q)
	{
		mesh->quads[q].mat_index = 0;
		mesh->quads[q].vertex_indices[0] = indices[q*4 + 0];
		mesh->quads[q].vertex_indices[1] = indices[q*4 + 1];
		mesh->quads[q].vertex_indices[2] = indices[q*4 + 2];
		mesh->quads[q].vertex_indices[3] = indices[q*4 + 3];
		mesh->quads[q].uv_indices[0] = indices[q*4 + 0];
		mesh->quads[q].uv_indices[1] = indices[q*4 + 1];
		mesh->quads[q].uv_indices[2] = indices[q*4 + 2];
		mesh->quads[q].uv_indices[3] = indices[q*4 + 3];
	}

	
	mesh->endOfModel();

	Reference<OpenGLMeshRenderData> opengl_mesh;// = GLMeshBuilding::buildIndigoMesh(&allocator, mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	results.indigo_mesh = mesh;
	return results;
}


MeshBuilding::MeshBuildingResults MeshBuilding::makeWedgeMesh(const std::string& base_dir_path, VertexBufferAllocator& allocator)
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 1;

	Indigo::Vector<Indigo::Vec3f> verts;
	//Indigo::Vector<Indigo::Vec3f> normals;
	Indigo::Vector<Indigo::Vec2f> uvs;
	Indigo::Vector<uint32> indices;

	verts.push_back(Indigo::Vec3f(-0.5f, -0.5f, -0.5f)); // front bot left   0 
	verts.push_back(Indigo::Vec3f( 0.5f, -0.5f, -0.5f)); // front bot right  1
	verts.push_back(Indigo::Vec3f( 0.5f, -0.5f,  0.5f)); // front top right  2

	verts.push_back(Indigo::Vec3f(-0.5f,  0.5f, -0.5f)); // back bot left    3
	verts.push_back(Indigo::Vec3f( 0.5f,  0.5f, -0.5f)); // back bot right   4
	verts.push_back(Indigo::Vec3f( 0.5f,  0.5f,  0.5f)); // back top right   5

	uvs.push_back(Indigo::Vec2f(0,0));
	uvs.push_back(Indigo::Vec2f(1,0));
	uvs.push_back(Indigo::Vec2f(1,1));
	uvs.push_back(Indigo::Vec2f(0,1));

	mesh->vert_positions = verts;
	//mesh->vert_normals = normals;
	mesh->uv_pairs = uvs;

	// Front tri
	{
		uint32 vert_indices[] = { 0, 1, 2 };
		uint32 uv_indices[] = {0, 1, 2};
		mesh->addTriangle(vert_indices, uv_indices, 0);
	}
	// back tri
	{
		uint32 vert_indices[] = { 3, 5, 4 };
		uint32 uv_indices[] = {1, 3, 0}; // CHECKME
		mesh->addTriangle(vert_indices, uv_indices, 0);
	}
	// bottom quad
	{
		uint32 vert_indices[] = { 0, 3, 4, 1 };
		uint32 uv_indices[] = {0, 1, 2, 3}; // CHECKME
		mesh->addQuad(vert_indices, uv_indices, 0);
	}
	// quad on y-z quad at x=0.5f: (right side)
	{
		uint32 vert_indices[] = { 1, 4, 5, 2 };
		uint32 uv_indices[] = {0, 1, 2, 3}; // CHECKME
		mesh->addQuad(vert_indices, uv_indices, 0);
	}
	// quad on slope
	{
		uint32 vert_indices[] = { 0, 2, 5, 3 };
		uint32 uv_indices[] = {1, 2, 3, 0}; // CHECKME
		mesh->addQuad(vert_indices, uv_indices, 0);
	}

	mesh->endOfModel();

	Reference<OpenGLMeshRenderData> opengl_mesh;// = GLMeshBuilding::buildIndigoMesh(&allocator, mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	results.indigo_mesh = mesh;
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


void MeshBuilding::test()
{
	conPrint("MeshBuilding::test()");

	VertexBufferAllocator allocator(/*use_grouped_vbo_allocator=*/false);

	{
		auto res = makeConeMesh(".", allocator);
		Indigo::Mesh::writeToFile("cone.igmesh", *res.indigo_mesh, /*use compression=*/false);
	}
	{
		auto res = makeWedgeMesh(".", allocator);
		Indigo::Mesh::writeToFile("wedge.igmesh", *res.indigo_mesh, /*use compression=*/false);
	}

	conPrint("MeshBuilding::test() done.");
}
