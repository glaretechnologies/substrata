/*=====================================================================
Parcel.cpp
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "Parcel.h"


#include <Exception.h>
#include <StringUtils.h>
#include <ContainerUtils.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#endif
#include "../gui_client/PhysicsObject.h"
#include <StandardPrintOutput.h>


Parcel::Parcel()
:	state(State_JustCreated),
	from_remote_dirty(false),
	from_local_dirty(false)
{
}


Parcel::~Parcel()
{

}


void Parcel::build() // Build cached data like aabb_min
{
	aabb_min.x = myMin(myMin(verts[0].x, verts[1].x), myMin(verts[2].x, verts[3].x));
	aabb_min.y = myMin(myMin(verts[0].y, verts[1].y), myMin(verts[2].y, verts[3].y));
	aabb_min.z = zbounds.x;
	aabb_max.x = myMax(myMax(verts[0].x, verts[1].x), myMax(verts[2].x, verts[3].x));
	aabb_max.y = myMax(myMax(verts[0].y, verts[1].y), myMax(verts[2].y, verts[3].y));
	aabb_max.z = zbounds.y;
}


bool Parcel::pointInParcel(const Vec3d& p) const
{
	return
		p.x >= aabb_min.x && p.y >= aabb_min.y && p.z >= aabb_min.z &&
		p.x <= aabb_max.x && p.y <= aabb_max.y && p.z <= aabb_max.z;
}


bool Parcel::AABBInParcel(const js::AABBox& aabb) const
{
	return AABBInParcelBounds(aabb, this->aabb_min, this->aabb_max);
}


bool Parcel::AABBInParcelBounds(const js::AABBox& aabb, const Vec3d& parcel_aabb_min, const Vec3d& parcel_aabb_max)
{
	return
		aabb.min_[0] >= parcel_aabb_min.x && aabb.min_[1] >= parcel_aabb_min.y && aabb.min_[2] >= parcel_aabb_min.z &&
		aabb.max_[0] <= parcel_aabb_max.x && aabb.max_[1] <= parcel_aabb_max.y && aabb.max_[2] <= parcel_aabb_max.z;
}


bool Parcel::isAxisAlignedBox() const
{
	return
		verts[0].x == aabb_min.x && verts[0].y == aabb_min.y &&
		verts[1].x == aabb_max.x && verts[1].y == aabb_min.y &&
		verts[2].x == aabb_max.x && verts[2].y == aabb_max.y &&
		verts[3].x == aabb_min.x && verts[3].y == aabb_max.y;
}


bool Parcel::userIsParcelAdmin(const UserID user_id) const
{
	return ContainerUtils::contains(admin_ids, user_id);
}


bool Parcel::userIsParcelWriter(const UserID user_id) const
{
	return ContainerUtils::contains(writer_ids, user_id);
}


#if GUI_CLIENT


static Colour3f colForPrivs(bool write_privileges)
{
	return write_privileges ? Colour3f(0.4f, 0.9f, 0.3f) : Colour3f(0.9f, 0.9f, 0.3f);
}


Reference<GLObject> Parcel::makeOpenGLObject(Reference<OpenGLEngine>& opengl_engine, bool write_privileges)
{
	const Vec4f aabb_min_v4(aabb_min.x, aabb_min.y, aabb_min.z, 1.0f);
	const Vec4f aabb_max_v4(aabb_max.x, aabb_max.y, aabb_max.z, 1.0f);

	const Colour3f col = colForPrivs(write_privileges);

	if(isAxisAlignedBox())
	{
		opengl_engine_ob = opengl_engine->makeAABBObject(aabb_min_v4, aabb_max_v4, Colour4f(col.r, col.g, col.b, 0.5f));
		return opengl_engine_ob;
	}
	else
	{
		Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();

		js::Vector<Vec3f, 16> mesh_verts;
		mesh_verts.resize(24); // 6 faces * 4 verts/face
		js::Vector<Vec3f, 16> normals;
		normals.resize(24);
		js::Vector<Vec2f, 16> uvs;
		uvs.resize(24);
		js::Vector<uint32, 16> indices;
		indices.resize(6 * 6); // two tris per face, 6 faces

		for(int face = 0; face < 6; ++face)
		{
			indices[face*6 + 0] = face*4 + 0;
			indices[face*6 + 1] = face*4 + 1;
			indices[face*6 + 2] = face*4 + 2;
			indices[face*6 + 3] = face*4 + 0;
			indices[face*6 + 4] = face*4 + 2;
			indices[face*6 + 5] = face*4 + 3;
		}

		// Sides of parcel
		for(int i=0; i<4; ++i)
		{
			Vec2d v = this->verts[i];
			Vec2d next_v = this->verts[(i + 1) % 4];

			Vec3f v0(v.x, v.y, zbounds.x);
			Vec3f v1(next_v.x, next_v.y, zbounds.x);
			Vec3f v2(next_v.x, next_v.y, zbounds.y);
			Vec3f v3(v.x, v.y, zbounds.y);

			const int face = i;
			mesh_verts[face*4 + 0] = v0;
			mesh_verts[face*4 + 1] = v1;
			mesh_verts[face*4 + 2] = v2;
			mesh_verts[face*4 + 3] = v3;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int z=0; z<4; ++z)
				normals[face*4 + z] = normalise(crossProduct(v1 - v0, v2 - v0));
		}

		// Bottom
		{
			Vec3f v0(verts[0].x, verts[0].y, zbounds.x);
			Vec3f v1(verts[3].x, verts[3].y, zbounds.x);
			Vec3f v2(verts[2].x, verts[2].y, zbounds.x);
			Vec3f v3(verts[1].x, verts[1].y, zbounds.x);

			const int face = 4;
			mesh_verts[face*4 + 0] = v0;
			mesh_verts[face*4 + 1] = v1;
			mesh_verts[face*4 + 2] = v2;
			mesh_verts[face*4 + 3] = v3;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int z=0; z<4; ++z)
				normals[face*4 + z] = normalise(crossProduct(v1 - v0, v2 - v0));
		}

		// Top
		{
			Vec3f v0(verts[0].x, verts[0].y, zbounds.y);
			Vec3f v1(verts[1].x, verts[1].y, zbounds.y);
			Vec3f v2(verts[2].x, verts[2].y, zbounds.y);
			Vec3f v3(verts[3].x, verts[3].y, zbounds.y);

			const int face = 5;
			mesh_verts[face*4 + 0] = v0;
			mesh_verts[face*4 + 1] = v1;
			mesh_verts[face*4 + 2] = v2;
			mesh_verts[face*4 + 3] = v3;

			uvs[face*4 + 0] = Vec2f(0, 0);
			uvs[face*4 + 1] = Vec2f(1, 0);
			uvs[face*4 + 2] = Vec2f(1, 1);
			uvs[face*4 + 3] = Vec2f(0, 1);

			for(int z=0; z<4; ++z)
				normals[face*4 + z] = normalise(crossProduct(v1 - v0, v2 - v0));
		}

		opengl_engine->buildMeshRenderData(*mesh_data, mesh_verts, normals, uvs, indices);

		Reference<GLObject> ob = new GLObject();

		ob->ob_to_world_matrix.setToIdentity();
		ob->mesh_data = mesh_data;
		ob->materials.resize(1);
		ob->materials[0].albedo_rgb = col;
		ob->materials[0].alpha = 0.5f;
		ob->materials[0].transparent = true;
		return ob;
	}
}


void Parcel::setColourForPerms(bool write_privileges)
{
	if(opengl_engine_ob.nonNull() && !opengl_engine_ob->materials.empty())
		opengl_engine_ob->materials[0].albedo_rgb = colForPrivs(write_privileges);
}


Reference<PhysicsObject> Parcel::makePhysicsObject(Reference<RayMesh>& unit_cube_raymesh, Indigo::TaskManager& task_manager)
{
	Reference<PhysicsObject> new_physics_object = new PhysicsObject(/*collidable=*/false);

	if(isAxisAlignedBox())
	{
		const Vec4f aabb_min_v4(aabb_min.x, aabb_min.y, aabb_min.z, 1.0f);
		const Vec4f aabb_max_v4(aabb_max.x, aabb_max.y, aabb_max.z, 1.0f);

		const Vec4f span = aabb_max_v4 - aabb_min_v4;

		Matrix4f ob_to_world_matrix;
		ob_to_world_matrix.setColumn(0, Vec4f(span[0], 0, 0, 0));
		ob_to_world_matrix.setColumn(1, Vec4f(0, span[1], 0, 0));
		ob_to_world_matrix.setColumn(2, Vec4f(0, 0, span[2], 0));
		ob_to_world_matrix.setColumn(3, aabb_min_v4); // set origin

		new_physics_object->geometry = unit_cube_raymesh;
		new_physics_object->ob_to_world = ob_to_world_matrix;
	}
	else
	{
		//mw.ground_quad_raymesh->fromIndigoMesh(*mw.ground_quad_mesh);
		Reference<RayMesh> mesh = new RayMesh("raymesh", false);

		mesh->getVertices().resize(24);
		mesh->getQuads().resize(6);

		// Sides of parcel
		for(int i=0; i<4; ++i)
		{
			Vec2d v = this->verts[i];
			Vec2d next_v = this->verts[(i + 1) % 4];

			Vec3f v0(v.x, v.y, zbounds.x);
			Vec3f v1(next_v.x, next_v.y, zbounds.x);
			Vec3f v2(next_v.x, next_v.y, zbounds.y);
			Vec3f v3(v.x, v.y, zbounds.y);

			const int face = i;
			mesh->getVertices()[face*4 + 0].pos = v0;
			mesh->getVertices()[face*4 + 1].pos = v1;
			mesh->getVertices()[face*4 + 2].pos = v2;
			mesh->getVertices()[face*4 + 3].pos = v3;

			mesh->getQuads()[face].vertex_indices[0] = face*4 + 0;
			mesh->getQuads()[face].vertex_indices[1] = face*4 + 1;
			mesh->getQuads()[face].vertex_indices[2] = face*4 + 2;
			mesh->getQuads()[face].vertex_indices[3] = face*4 + 3;
			for(int c=0; c<4; ++c)
				mesh->getQuads()[face].uv_indices[c] = 0;
		}

		// Bottom
		{
			Vec3f v0(verts[0].x, verts[0].y, zbounds.x);
			Vec3f v1(verts[3].x, verts[3].y, zbounds.x);
			Vec3f v2(verts[2].x, verts[2].y, zbounds.x);
			Vec3f v3(verts[1].x, verts[1].y, zbounds.x);

			const int face = 4;
			mesh->getVertices()[face*4 + 0].pos = v0;
			mesh->getVertices()[face*4 + 1].pos = v1;
			mesh->getVertices()[face*4 + 2].pos = v2;
			mesh->getVertices()[face*4 + 3].pos = v3;

			mesh->getQuads()[face].vertex_indices[0] = face*4 + 0;
			mesh->getQuads()[face].vertex_indices[1] = face*4 + 1;
			mesh->getQuads()[face].vertex_indices[2] = face*4 + 2;
			mesh->getQuads()[face].vertex_indices[3] = face*4 + 3;
			for(int c=0; c<4; ++c)
				mesh->getQuads()[face].uv_indices[c] = 0;
		}

		// Top
		{
			Vec3f v0(verts[0].x, verts[0].y, zbounds.y);
			Vec3f v1(verts[1].x, verts[1].y, zbounds.y);
			Vec3f v2(verts[2].x, verts[2].y, zbounds.y);
			Vec3f v3(verts[3].x, verts[3].y, zbounds.y);

			const int face = 5;
			mesh->getVertices()[face*4 + 0].pos = v0;
			mesh->getVertices()[face*4 + 1].pos = v1;
			mesh->getVertices()[face*4 + 2].pos = v2;
			mesh->getVertices()[face*4 + 3].pos = v3;

			mesh->getQuads()[face].vertex_indices[0] = face*4 + 0;
			mesh->getQuads()[face].vertex_indices[1] = face*4 + 1;
			mesh->getQuads()[face].vertex_indices[2] = face*4 + 2;
			mesh->getQuads()[face].vertex_indices[3] = face*4 + 3;
			for(int c=0; c<4; ++c)
				mesh->getQuads()[face].uv_indices[c] = 0;
		}

		mesh->buildTrisFromQuads();
		Geometry::BuildOptions options;
		StandardPrintOutput print_output;
		mesh->build(".", options, print_output, /*verbose=*/false, task_manager);

		new_physics_object->geometry = mesh;
		new_physics_object->ob_to_world = Matrix4f::identity();
	}

	new_physics_object->userdata = this;
	new_physics_object->userdata_type = 1;
	return new_physics_object;
}


#endif // GUI_CLIENT


static const uint32 PARCEL_SERIALISATION_VERSION = 2;


static void writeToStreamCommon(const Parcel& parcel, OutStream& stream)
{
	writeToStream(parcel.id, stream);
	writeToStream(parcel.owner_id, stream);
	parcel.created_time.writeToStream(stream);
	stream.writeStringLengthFirst(parcel.description);
	
	// Write admin_ids
	stream.writeUInt32((uint32)parcel.admin_ids.size());
	for(size_t i=0; i<parcel.admin_ids.size(); ++i)
		writeToStream(parcel.admin_ids[i], stream);

	// Write writer_ids
	stream.writeUInt32((uint32)parcel.writer_ids.size());
	for(size_t i=0; i<parcel.writer_ids.size(); ++i)
		writeToStream(parcel.writer_ids[i], stream);

	// Write child_parcel_ids
	stream.writeUInt32((uint32)parcel.child_parcel_ids.size());
	for(size_t i=0; i<parcel.child_parcel_ids.size(); ++i)
		writeToStream(parcel.child_parcel_ids[i], stream);

	for(int i=0; i<4; ++i)
		writeToStream(parcel.verts[i], stream);
	writeToStream(parcel.zbounds, stream);
}


static void readFromStreamCommon(InStream& stream, Parcel& parcel) // UID will have been read already
{
	parcel.owner_id = readUserIDFromStream(stream);
	parcel.created_time.readFromStream(stream);
	parcel.description = stream.readStringLengthFirst(10000);

	// Read admin_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many admin_ids: " + toString(num));
		parcel.admin_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_ids[i] = readUserIDFromStream(stream);
	}

	// Read writer_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many writer_ids: " + toString(num));
		parcel.writer_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_ids[i] = readUserIDFromStream(stream);
	}

	// Read child_parcel_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many child_parcel_ids: " + toString(num));
		parcel.child_parcel_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.child_parcel_ids[i] = readParcelIDFromStream(stream);
	}

	for(int i=0; i<4; ++i)
		parcel.verts[i] = readVec2FromStream<double>(stream);
	parcel.zbounds = readVec2FromStream<double>(stream);

	parcel.build();
}


void writeToStream(const Parcel& parcel, OutStream& stream)
{
	// Write version
	stream.writeUInt32(PARCEL_SERIALISATION_VERSION);

	writeToStreamCommon(parcel, stream);
}


void readFromStream(InStream& stream, Parcel& parcel)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > PARCEL_SERIALISATION_VERSION)
		throw Indigo::Exception("Parcel readFromStream: Unsupported version " + toString(v) + ", expected " + toString(PARCEL_SERIALISATION_VERSION) + ".");

	parcel.id = readParcelIDFromStream(stream);
	
	readFromStreamCommon(stream, parcel);
}


void writeToNetworkStream(const Parcel& parcel, OutStream& stream)
{
	writeToStreamCommon(parcel, stream);

	stream.writeStringLengthFirst(parcel.owner_name);

	// Write admin_names
	stream.writeUInt32((uint32)parcel.admin_names.size());
	for(size_t i=0; i<parcel.admin_names.size(); ++i)
		stream.writeStringLengthFirst(parcel.admin_names[i]);

	// Write writer_names
	stream.writeUInt32((uint32)parcel.writer_names.size());
	for(size_t i=0; i<parcel.writer_names.size(); ++i)
		stream.writeStringLengthFirst(parcel.writer_names[i]);
}


void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel) // UID will have been read already
{
	readFromStreamCommon(stream, parcel);

	parcel.owner_name = stream.readStringLengthFirst(10000);

	// Read admin_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw Indigo::Exception("Too many admin_names: " + toString(num));
		parcel.admin_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}

	// Read writer_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw Indigo::Exception("Too many writer_names: " + toString(num));
		parcel.writer_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}

	parcel.build();
}
