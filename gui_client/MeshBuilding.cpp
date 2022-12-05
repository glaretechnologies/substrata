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


MeshBuilding::MeshBuildingResults MeshBuilding::makeImageCube(glare::TaskManager& task_manager, VertexBufferAllocator& allocator)
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
	results.physics_shape.jolt_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh);
	results.indigo_mesh = mesh;
	return results;
}


MeshBuilding::MeshBuildingResults MeshBuilding::makeSpotlightMeshes(const std::string& base_dir_path, glare::TaskManager& task_manager, VertexBufferAllocator& allocator)
{
	const std::string model_path = base_dir_path + "/resources/spotlight5.glb";

	GLTFLoadedData gltf_data;
	BatchedMeshRef batched_mesh = FormatDecoderGLTF::loadGLBFile(model_path, gltf_data);

	batched_mesh->checkValidAndSanitiseMesh();

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh = GLMeshBuilding::buildBatchedMesh(&allocator, batched_mesh, /*skip opengl calls=*/false, /*instancing_matrix_data=*/NULL); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = spotlight_opengl_mesh;
	results.physics_shape.jolt_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*batched_mesh);
	return results;

#if 0
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

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh = GLMeshBuilding::buildIndigoMesh(&allocator, spotlight_mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

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
#endif
}


PhysicsShape MeshBuilding::makeUnitCubePhysicsShape(glare::TaskManager& task_manager, VertexBufferAllocator& allocator)
{
	Indigo::MeshRef mesh = new Indigo::Mesh();
	mesh->num_uv_mappings = 0;

	// The y=0 and y=1 faces are the ones the image is actually applied to.

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

	PhysicsShape physics_shape;
	physics_shape.jolt_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh);
	return physics_shape;
}


Reference<OpenGLMeshRenderData> MeshBuilding::makeRotationArcHandleMeshData(VertexBufferAllocator& allocator, float arc_end_angle)
{
	const int arc_res = 32;
	const int res = 20; // Number of vertices round cylinder

	js::Vector<Vec3f, 16> verts;
	verts.resize(res * arc_res * 4 + res * 4 * 2); // Arrow head is res * 4 verts, 2 arrow heads.
	js::Vector<Vec3f, 16> normals;
	normals.resize(res * arc_res * 4 + res * 4 * 2);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(res * arc_res * 4 + res * 4 * 2);
	js::Vector<uint32, 16> indices;
	indices.resize(res * arc_res * 6 + res * 6 * 2); // two tris per quad

	const Vec4f basis_j(0,0,1,0);

	const float cyl_r = 0.02f;
	const float head_len = 0.2f;
	const float head_r = 0.04f;

	const float arc_start_angle = 0;

	for(int z=0; z<arc_res; ++z)
	{
		const float arc_angle_0 = z       * (arc_end_angle - arc_start_angle) / arc_res;
		const float arc_angle_1 = (z + 1) * (arc_end_angle - arc_start_angle) / arc_res;

		Vec4f cyl_centre_0(cos(arc_angle_0), sin(arc_angle_0), 0, 1);
		Vec4f cyl_centre_1(cos(arc_angle_1), sin(arc_angle_1), 0, 1);

		Vec4f basis_i_0 = normalise(cyl_centre_0 - Vec4f(0,0,0,1));
		Vec4f basis_i_1 = normalise(cyl_centre_1 - Vec4f(0,0,0,1));

		// Draw cylinder for shaft of arrow
		for(int i=0; i<res; ++i)
		{
			const float angle_0 = i       * Maths::get2Pi<float>() / res;
			const float angle_1 = (i + 1) * Maths::get2Pi<float>() / res;

			// Define quad
			{
				normals[(z*res + i)*4 + 0] = toVec3f(basis_i_0 * cos(angle_0) + basis_j * sin(angle_0));
				normals[(z*res + i)*4 + 1] = toVec3f(basis_i_0 * cos(angle_1) + basis_j * sin(angle_1));
				normals[(z*res + i)*4 + 2] = toVec3f(basis_i_1 * cos(angle_1) + basis_j * sin(angle_1));
				normals[(z*res + i)*4 + 3] = toVec3f(basis_i_1 * cos(angle_0) + basis_j * sin(angle_0));

				Vec4f v0 = cyl_centre_0 + (basis_i_0 * cos(angle_0) + basis_j * sin(angle_0)) * cyl_r;
				Vec4f v1 = cyl_centre_0 + (basis_i_0 * cos(angle_1) + basis_j * sin(angle_1)) * cyl_r;
				Vec4f v2 = cyl_centre_1 + (basis_i_1 * cos(angle_1) + basis_j * sin(angle_1)) * cyl_r;
				Vec4f v3 = cyl_centre_1 + (basis_i_1 * cos(angle_0) + basis_j * sin(angle_0)) * cyl_r;

				verts[(z*res + i)*4 + 0] = toVec3f(v0);
				verts[(z*res + i)*4 + 1] = toVec3f(v1);
				verts[(z*res + i)*4 + 2] = toVec3f(v2);
				verts[(z*res + i)*4 + 3] = toVec3f(v3);

				uvs[(z*res + i)*4 + 0] = Vec2f(0.f);
				uvs[(z*res + i)*4 + 1] = Vec2f(0.f);
				uvs[(z*res + i)*4 + 2] = Vec2f(0.f);
				uvs[(z*res + i)*4 + 3] = Vec2f(0.f);

				indices[(z*res + i)*6 + 0] = (z*res + i)*4 + 0; 
				indices[(z*res + i)*6 + 1] = (z*res + i)*4 + 1; 
				indices[(z*res + i)*6 + 2] = (z*res + i)*4 + 2; 
				indices[(z*res + i)*6 + 3] = (z*res + i)*4 + 0;
				indices[(z*res + i)*6 + 4] = (z*res + i)*4 + 2;
				indices[(z*res + i)*6 + 5] = (z*res + i)*4 + 3;
			}
		}
	}

	// Add arrow head at arc start
	{
		Vec4f cyl_centre_0(cos(arc_start_angle), sin(arc_start_angle), 0, 1);

		Vec4f basis_i = normalise(cyl_centre_0 - Vec4f(0,0,0,1));

		const Vec4f dir = crossProduct(basis_i, basis_j);

		int v_offset = res * arc_res * 4;
		int i_offset = res * arc_res * 6;
		for(int i=0; i<res; ++i)
		{
			const float angle      = i       * Maths::get2Pi<float>() / res;
			const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

			// Define arrow head
			{
				// NOTE: this normal is somewhat wrong.
				Vec4f normal1(basis_i * cos(angle     ) + basis_j * sin(angle     ));
				Vec4f normal2(basis_i * cos(next_angle) + basis_j * sin(next_angle));

				normals[v_offset + i*4 + 0] = toVec3f(normal1);
				normals[v_offset + i*4 + 1] = toVec3f(normal2);
				normals[v_offset + i*4 + 2] = toVec3f(normal2);
				normals[v_offset + i*4 + 3] = toVec3f(normal1);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v2 = cyl_centre_0 + (dir * head_len);
				Vec4f v3 = cyl_centre_0 + (dir * head_len);

				verts[v_offset + i*4 + 0] = toVec3f(v0);
				verts[v_offset + i*4 + 1] = toVec3f(v1);
				verts[v_offset + i*4 + 2] = toVec3f(v2);
				verts[v_offset + i*4 + 3] = toVec3f(v3);

				uvs[v_offset + i*4 + 0] = Vec2f(0.f);
				uvs[v_offset + i*4 + 1] = Vec2f(0.f);
				uvs[v_offset + i*4 + 2] = Vec2f(0.f);
				uvs[v_offset + i*4 + 3] = Vec2f(0.f);

				indices[i_offset + i*6 + 0] = v_offset + i*4 + 0; 
				indices[i_offset + i*6 + 1] = v_offset + i*4 + 1; 
				indices[i_offset + i*6 + 2] = v_offset + i*4 + 2; 
				indices[i_offset + i*6 + 3] = v_offset + i*4 + 0;
				indices[i_offset + i*6 + 4] = v_offset + i*4 + 2;
				indices[i_offset + i*6 + 5] = v_offset + i*4 + 3;
			}
		}
	}

	// Add arrow head at arc end
	{
		Vec4f cyl_centre_0(cos(arc_end_angle), sin(arc_end_angle), 0, 1);

		Vec4f basis_i = normalise(cyl_centre_0 - Vec4f(0,0,0,1));

		const Vec4f dir = -crossProduct(basis_i, basis_j);

		int v_offset = res * arc_res * 4 + res * 4;
		int i_offset = res * arc_res * 6 + res * 6;
		for(int i=0; i<res; ++i)
		{
			const float angle      = i       * Maths::get2Pi<float>() / res;
			const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

			// Define arrow head
			{
				// NOTE: this normal is somewhat wrong.
				Vec4f normal1(basis_i * cos(angle     ) + basis_j * sin(angle     ));
				Vec4f normal2(basis_i * cos(next_angle) + basis_j * sin(next_angle));

				normals[v_offset + i*4 + 0] = toVec3f(normal1);
				normals[v_offset + i*4 + 1] = toVec3f(normal2);
				normals[v_offset + i*4 + 2] = toVec3f(normal2);
				normals[v_offset + i*4 + 3] = toVec3f(normal1);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v2 = cyl_centre_0 + (dir * head_len);
				Vec4f v3 = cyl_centre_0 + (dir * head_len);

				verts[v_offset + i*4 + 0] = toVec3f(v0);
				verts[v_offset + i*4 + 1] = toVec3f(v1);
				verts[v_offset + i*4 + 2] = toVec3f(v2);
				verts[v_offset + i*4 + 3] = toVec3f(v3);

				uvs[v_offset + i*4 + 0] = Vec2f(0.f);
				uvs[v_offset + i*4 + 1] = Vec2f(0.f);
				uvs[v_offset + i*4 + 2] = Vec2f(0.f);
				uvs[v_offset + i*4 + 3] = Vec2f(0.f);

				indices[i_offset + i*6 + 0] = v_offset + i*4 + 0; 
				indices[i_offset + i*6 + 1] = v_offset + i*4 + 1; 
				indices[i_offset + i*6 + 2] = v_offset + i*4 + 2; 
				indices[i_offset + i*6 + 3] = v_offset + i*4 + 0;
				indices[i_offset + i*6 + 4] = v_offset + i*4 + 2;
				indices[i_offset + i*6 + 5] = v_offset + i*4 + 3;
			}
		}
	}

	return OpenGLEngine::buildMeshRenderData(allocator, verts, normals, uvs, indices);
}
