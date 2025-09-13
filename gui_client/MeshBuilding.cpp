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


MeshBuilding::MeshBuildingResults MeshBuilding::makeSpotlightMeshes(const std::string& base_dir_path, VertexBufferAllocator& allocator)
{
	const std::string model_path = base_dir_path + "/data/resources/spotlight5.glb";

	GLTFLoadedData gltf_data;
	BatchedMeshRef batched_mesh = FormatDecoderGLTF::loadGLBFile(model_path, gltf_data);

	batched_mesh->checkValidAndSanitiseMesh();

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh = GLMeshBuilding::buildBatchedMesh(&allocator, batched_mesh, /*skip opengl calls=*/false); // Build OpenGLMeshRenderData

	MeshBuildingResults results;
	results.opengl_mesh_data = spotlight_opengl_mesh;
	results.physics_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*batched_mesh, /*is dynamic=*/false);
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


// See MeshPrimitiveBuilding::make3DArrowMesh()
Reference<OpenGLMeshRenderData> MeshBuilding::makeRotationArcHandleMeshData(VertexBufferAllocator& allocator, float arc_end_angle)
{
	const int arc_res = 32;
	const int res = 20; // Number of vertices around cylinder


	js::Vector<Vec3f, 16> verts;
	verts.resize(res * arc_res * 4 + res * 8 * 2); // Arrow head is res * 8 verts, 2 arrow heads.
	js::Vector<Vec3f, 16> normals;
	normals.resize(res * arc_res * 4 + res * 8 * 2);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(res * arc_res * 4 + res * 8 * 2);
	js::Vector<uint32, 16> indices;
	indices.resize(res * arc_res * 6 + res * 12 * 2); // two tris per quad

	const Vec4f basis_j(0,0,1,0);

	const float cyl_r = 0.02f;
	const float head_len = 0.2f;
	const float head_r = 0.04f; // Radius at base of head

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

				normals[v_offset + i*8 + 0] = toVec3f(normal1);
				normals[v_offset + i*8 + 1] = toVec3f(normal2);
				normals[v_offset + i*8 + 2] = toVec3f(normal2);
				normals[v_offset + i*8 + 3] = toVec3f(normal1);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v2 = cyl_centre_0 + (dir * head_len);
				Vec4f v3 = cyl_centre_0 + (dir * head_len);

				verts[v_offset + i*8 + 0] = toVec3f(v0);
				verts[v_offset + i*8 + 1] = toVec3f(v1);
				verts[v_offset + i*8 + 2] = toVec3f(v2);
				verts[v_offset + i*8 + 3] = toVec3f(v3);

				uvs[v_offset + i*8 + 0] = Vec2f(0.f);
				uvs[v_offset + i*8 + 1] = Vec2f(0.f);
				uvs[v_offset + i*8 + 2] = Vec2f(0.f);
				uvs[v_offset + i*8 + 3] = Vec2f(0.f);

				indices[i_offset + i*12 + 0] = v_offset + i*8 + 0; 
				indices[i_offset + i*12 + 1] = v_offset + i*8 + 1; 
				indices[i_offset + i*12 + 2] = v_offset + i*8 + 2; 
				indices[i_offset + i*12 + 3] = v_offset + i*8 + 0;
				indices[i_offset + i*12 + 4] = v_offset + i*8 + 2;
				indices[i_offset + i*12 + 5] = v_offset + i*8 + 3;
			}

			// Draw disc on bottom of arrow head
			{
				Vec4f normal = -dir;

				normals[v_offset + i*8 + 4] = toVec3f(normal);
				normals[v_offset + i*8 + 5] = toVec3f(normal);
				normals[v_offset + i*8 + 6] = toVec3f(normal);
				normals[v_offset + i*8 + 7] = toVec3f(normal);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v2 = cyl_centre_0;
				Vec4f v3 = cyl_centre_0;

				verts[v_offset + i*8 + 4] = toVec3f(v0);
				verts[v_offset + i*8 + 5] = toVec3f(v1);
				verts[v_offset + i*8 + 6] = toVec3f(v2);
				verts[v_offset + i*8 + 7] = toVec3f(v3);

				uvs[v_offset + i*8 + 4] = Vec2f(0.f);
				uvs[v_offset + i*8 + 5] = Vec2f(0.f);
				uvs[v_offset + i*8 + 6] = Vec2f(0.f);
				uvs[v_offset + i*8 + 7] = Vec2f(0.f);

				indices[i_offset + i*12 +  6] = v_offset + i*8 + 4; 
				indices[i_offset + i*12 +  7] = v_offset + i*8 + 5; 
				indices[i_offset + i*12 +  8] = v_offset + i*8 + 6; 
				indices[i_offset + i*12 +  9] = v_offset + i*8 + 4;
				indices[i_offset + i*12 + 10] = v_offset + i*8 + 6;
				indices[i_offset + i*12 + 11] = v_offset + i*8 + 7;
			}
		}
	}

	// Add arrow head at arc end
	{
		Vec4f cyl_centre_0(cos(arc_end_angle), sin(arc_end_angle), 0, 1);

		Vec4f basis_i = normalise(cyl_centre_0 - Vec4f(0,0,0,1));

		const Vec4f dir = -crossProduct(basis_i, basis_j);

		int v_offset = res * arc_res * 4 + res * 8;
		int i_offset = res * arc_res * 6 + res * 12;
		for(int i=0; i<res; ++i)
		{
			const float angle      = i       * Maths::get2Pi<float>() / res;
			const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

			// Define arrow head
			{
				// NOTE: this normal is somewhat wrong.
				Vec4f normal1(basis_i * cos(angle     ) + basis_j * sin(angle     ));
				Vec4f normal2(basis_i * cos(next_angle) + basis_j * sin(next_angle));

				normals[v_offset + i*8 + 0] = toVec3f(normal1);
				normals[v_offset + i*8 + 1] = toVec3f(normal2);
				normals[v_offset + i*8 + 2] = toVec3f(normal2);
				normals[v_offset + i*8 + 3] = toVec3f(normal1);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v2 = cyl_centre_0 + (dir * head_len);
				Vec4f v3 = cyl_centre_0 + (dir * head_len);

				verts[v_offset + i*8 + 0] = toVec3f(v0);
				verts[v_offset + i*8 + 1] = toVec3f(v1);
				verts[v_offset + i*8 + 2] = toVec3f(v2);
				verts[v_offset + i*8 + 3] = toVec3f(v3);

				uvs[v_offset + i*8 + 0] = Vec2f(0.f);
				uvs[v_offset + i*8 + 1] = Vec2f(0.f);
				uvs[v_offset + i*8 + 2] = Vec2f(0.f);
				uvs[v_offset + i*8 + 3] = Vec2f(0.f);

				indices[i_offset + i*12 + 0] = v_offset + i*8 + 0; 
				indices[i_offset + i*12 + 1] = v_offset + i*8 + 1; 
				indices[i_offset + i*12 + 2] = v_offset + i*8 + 2; 
				indices[i_offset + i*12 + 3] = v_offset + i*8 + 0;
				indices[i_offset + i*12 + 4] = v_offset + i*8 + 2;
				indices[i_offset + i*12 + 5] = v_offset + i*8 + 3;
			}

			// Draw disc on bottom of arrow head
			{
				Vec4f normal = -dir;

				normals[v_offset + i*8 + 4] = toVec3f(normal);
				normals[v_offset + i*8 + 5] = toVec3f(normal);
				normals[v_offset + i*8 + 6] = toVec3f(normal);
				normals[v_offset + i*8 + 7] = toVec3f(normal);

				Vec4f v0 = cyl_centre_0 + ((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r);
				Vec4f v1 = cyl_centre_0 + ((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r);
				Vec4f v2 = cyl_centre_0;
				Vec4f v3 = cyl_centre_0;

				verts[v_offset + i*8 + 4] = toVec3f(v0);
				verts[v_offset + i*8 + 5] = toVec3f(v1);
				verts[v_offset + i*8 + 6] = toVec3f(v2);
				verts[v_offset + i*8 + 7] = toVec3f(v3);

				uvs[v_offset + i*8 + 4] = Vec2f(0.f);
				uvs[v_offset + i*8 + 5] = Vec2f(0.f);
				uvs[v_offset + i*8 + 6] = Vec2f(0.f);
				uvs[v_offset + i*8 + 7] = Vec2f(0.f);

				indices[i_offset + i*12 +  6] = v_offset + i*8 + 4; 
				indices[i_offset + i*12 +  7] = v_offset + i*8 + 5; 
				indices[i_offset + i*12 +  8] = v_offset + i*8 + 6; 
				indices[i_offset + i*12 +  9] = v_offset + i*8 + 4;
				indices[i_offset + i*12 + 10] = v_offset + i*8 + 6;
				indices[i_offset + i*12 + 11] = v_offset + i*8 + 7;
			}
		}
	}

	return OpenGLEngine::buildMeshRenderData(allocator, verts, normals, uvs, indices);
}
