/*=====================================================================
Parcel.cpp
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "Parcel.h"


#include "Protocol.h"
#include <Exception.h>
#include <StringUtils.h>
#include <ContainerUtils.h>
#include <ConPrint.h>
#include <ShouldCancelCallback.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "opengl/OpenGLMeshRenderData.h"
#include "simpleraytracer/raymesh.h"
#include "../gui_client/PhysicsObject.h"
#include "../gui_client/PhysicsWorld.h"
#include "../dll/include/IndigoMesh.h"
#endif
#include <StandardPrintOutput.h>


Parcel::Parcel()
:	state(State_JustCreated),
	from_remote_dirty(false),
	from_local_dirty(false),
	all_writeable(false),
	nft_status(NFTStatus_NotNFT),
	minting_transaction_id(std::numeric_limits<uint64>::max()),
	flags(0),
	spawn_point(1000000000.000000)
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

	aabb = js::AABBox(
		Vec4f((float)aabb_min.x, (float)aabb_min.y, (float)aabb_min.z, 1.f),
		Vec4f((float)aabb_max.x, (float)aabb_max.y, (float)aabb_max.z, 1.f)
	);
}


bool Parcel::pointInParcel(const Vec3d& p) const
{
	return
		p.x >= aabb_min.x && p.y >= aabb_min.y && p.z >= aabb_min.z &&
		p.x <= aabb_max.x && p.y <= aabb_max.y && p.z <= aabb_max.z;
}


bool Parcel::AABBInParcel(const js::AABBox& other_aabb) const
{
	return AABBInParcelBounds(other_aabb, this->aabb_min, this->aabb_max);
}


bool Parcel::AABBIntersectsParcel(const js::AABBox& other_aabb) const
{
	return this->aabb.intersectsAABB(other_aabb);
}


bool Parcel::AABBInParcelBounds(const js::AABBox& aabb, const Vec3d& parcel_aabb_min, const Vec3d& parcel_aabb_max)
{
	return
		aabb.min_[0] >= parcel_aabb_min.x && aabb.min_[1] >= parcel_aabb_min.y && aabb.min_[2] >= parcel_aabb_min.z &&
		aabb.max_[0] <= parcel_aabb_max.x && aabb.max_[1] <= parcel_aabb_max.y && aabb.max_[2] <= parcel_aabb_max.z;
}


inline static uint32 mod4(uint32 x)
{
	return x & 0x3;
}


bool Parcel::isAxisAlignedBox() const
{
	// Work out which corner, if any, vert 0 is, and therefore the index of the vert at the lower left (min x and min y)
	// NOTE: this assume counter-clockwise vertex ordering which is not actually the case for some central parcels. (they were mirrored)
	uint32 lower_left_vert_i;
	if(verts[0].x == aabb_min.x)
	{
		if(verts[0].y == aabb_min.y)
			lower_left_vert_i = 0;
		else if(verts[0].y == aabb_max.y) // vert 0 is at top left
			lower_left_vert_i = 1;
		else
			return false; // Vert 0 is not in a corner.
	}
	else if(verts[0].x == aabb_max.x)
	{
		if(verts[0].y == aabb_min.y) // vert 0 is at bot right
			lower_left_vert_i = 3;
		else if(verts[0].y == aabb_max.y) // vert 0 is at top right
			lower_left_vert_i = 2;
		else
			return false; // Vert 0 is not in a corner.
	}
	else
		return false; // Vert 0 is not in a corner.

	return
		verts[mod4(lower_left_vert_i + 0)].x == aabb_min.x && verts[mod4(lower_left_vert_i + 0)].y == aabb_min.y &&
		verts[mod4(lower_left_vert_i + 1)].x == aabb_max.x && verts[mod4(lower_left_vert_i + 1)].y == aabb_min.y &&
		verts[mod4(lower_left_vert_i + 2)].x == aabb_max.x && verts[mod4(lower_left_vert_i + 2)].y == aabb_max.y &&
		verts[mod4(lower_left_vert_i + 3)].x == aabb_min.x && verts[mod4(lower_left_vert_i + 3)].y == aabb_max.y;
}


void Parcel::getScreenShotPosAndAngles(Vec3d& pos_out, Vec3d& angles_out) const
{
	const Vec3d parcel_centre = (aabb_min + aabb_max) * 0.5;

	pos_out = parcel_centre + Vec3d(-30, -30, 30);

	angles_out = Vec3d(/*heading=*/Maths::pi_4<double>(), /*pitch=*/2.2, /*roll=*/0);
}


void Parcel::getFarScreenShotPosAndAngles(Vec3d& pos_out, Vec3d& angles_out) const
{
	const Vec3d parcel_centre = (aabb_min + aabb_max) * 0.5;

	pos_out = parcel_centre + Vec3d(-0.1, -0.1, 500);

	angles_out = Vec3d(/*heading=*/Maths::pi_2<double>(), /*pitch=*/Maths::pi<double>() - 0.001, /*roll=*/0);
}


Vec3d Parcel::getVisitPosition() const
{
	if(std::fabs(spawn_point.x) < 100000.000000) // If spawn point has been set:
		return spawn_point;
	else
	{
		const Vec3d parcel_centre = (aabb_min + aabb_max) * 0.5;

		// Make sure at least 2m off ground
		Vec3d p = parcel_centre;
		p.z = myMax(p.z, 2.0);

		return p;
	}
}


bool Parcel::isAdjacentTo(const Parcel& other) const
{
	// Test along four edges and see if the minbounds = maxbounds of the other parcel.
	/*
	y
	^
	|    
	|    |   other    |
	|    |            |
	|    --------------
	|    |            |
	|    |   this     |
	|    
	-------------------> x
	*/

	// Test top edge
	if(aabb_min.x == other.aabb_min.x && aabb_max.y == other.aabb_min.y)
	{
		assert(aabb_max.x == other.aabb_max.x);
		return true;
	}

	// Test bottom edge
	if(aabb_min.x == other.aabb_min.x && aabb_min.y == other.aabb_max.y)
	{
		assert(aabb_max.x == other.aabb_max.x);
		return true;
	}

	// Test left edge
	if(aabb_min.y == other.aabb_min.y && aabb_min.x == other.aabb_max.x)
	{
		assert(aabb_max.y == other.aabb_max.y);
		return true;
	}

	// Test right edge
	if(aabb_min.y == other.aabb_min.y && aabb_max.x == other.aabb_min.x)
	{
		assert(aabb_max.y == other.aabb_max.y);
		return true;
	}

	return false;
}


std::string Parcel::districtName() const
{
	if(id.value() <= 425)
		return "Central";
	else if(id.value() <= 726)
		return "Market";
	else if(id.value() <= 953)
		return "East";
	else if(id.value() <= 1221)
		return "North";
	else
		return "Other";
}


bool Parcel::userIsParcelAdmin(const UserID user_id) const
{
	return ContainerUtils::contains(admin_ids, user_id);
}


bool Parcel::userIsParcelWriter(const UserID user_id) const
{
	return ContainerUtils::contains(writer_ids, user_id);
}


bool Parcel::userHasWritePerms(const UserID user_id) const // Does the user given by user_id have write permissions for this parcel?  E.g. are they an admin or writer?
{
	return (user_id == this->owner_id) || userIsParcelWriter(user_id) || userIsParcelAdmin(user_id) || (this->all_writeable && user_id.valid());
}


#if GUI_CLIENT


static Colour3f colForPrivs(bool write_privileges, bool is_admin_owned)
{
	//return write_privileges ? Colour3f(0.4f, 0.9f, 0.3f) : Colour3f(0.9f, 0.9f, 0.3f);
	return write_privileges ? Colour3f(0.2f, 0.8f, 0.3f) : 
		(is_admin_owned ? Colour3f(15/255.f, 156/255.f, 170/255.f) : Colour3f(0.1f, 0.4f, 0.8f)); // Colour slightly differently if owner is super-admin
}


Reference<GLObject> Parcel::makeOpenGLObject(Reference<OpenGLEngine>& opengl_engine, bool write_privileges)
{
	const Vec4f aabb_min_v4((float)aabb_min.x, (float)aabb_min.y, (float)aabb_min.z, 1.0f);
	const Vec4f aabb_max_v4((float)aabb_max.x, (float)aabb_max.y, (float)aabb_max.z, 1.0f);

	const Colour3f col = colForPrivs(write_privileges, /*is_admin_owned=*/owner_id == UserID(0));

	if(isAxisAlignedBox())
	{
		opengl_engine_ob = opengl_engine->makeAABBObject(aabb_min_v4, aabb_max_v4, Colour4f(col.r, col.g, col.b, 0.5f));
		return opengl_engine_ob;
	}
	else
	{
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

			Vec3f v0((float)v.x,		(float)v.y,			(float)zbounds.x);
			Vec3f v1((float)next_v.x,	(float)next_v.y,	(float)zbounds.x);
			Vec3f v2((float)next_v.x,	(float)next_v.y,	(float)zbounds.y);
			Vec3f v3((float)v.x,		(float)v.y,			(float)zbounds.y);

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
			Vec3f v0((float)verts[0].x, (float)verts[0].y, (float)zbounds.x);
			Vec3f v1((float)verts[3].x, (float)verts[3].y, (float)zbounds.x);
			Vec3f v2((float)verts[2].x, (float)verts[2].y, (float)zbounds.x);
			Vec3f v3((float)verts[1].x, (float)verts[1].y, (float)zbounds.x);

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
			Vec3f v0((float)verts[0].x, (float)verts[0].y, (float)zbounds.y);
			Vec3f v1((float)verts[1].x, (float)verts[1].y, (float)zbounds.y);
			Vec3f v2((float)verts[2].x, (float)verts[2].y, (float)zbounds.y);
			Vec3f v3((float)verts[3].x, (float)verts[3].y, (float)zbounds.y);

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

		Reference<OpenGLMeshRenderData> mesh_data = opengl_engine->buildMeshRenderData(*opengl_engine->vert_buf_allocator, mesh_verts, normals, uvs, indices);

		Reference<GLObject> ob = opengl_engine->allocateObject();

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
		opengl_engine_ob->materials[0].albedo_rgb = colForPrivs(write_privileges, /*is_admin_owned=*/owner_id == UserID(0));
}


Reference<PhysicsObject> Parcel::makePhysicsObject(PhysicsShape& unit_cube_shape, glare::TaskManager& task_manager)
{
	Reference<PhysicsObject> new_physics_object = new PhysicsObject(/*collidable=*/false);

	if(isAxisAlignedBox())
	{
		const Vec4f aabb_min_v4((float)aabb_min.x, (float)aabb_min.y, (float)aabb_min.z, 1.0f);
		const Vec4f aabb_max_v4((float)aabb_max.x, (float)aabb_max.y, (float)aabb_max.z, 1.0f);

		const Vec4f span = aabb_max_v4 - aabb_min_v4;

		new_physics_object->shape = unit_cube_shape;
		new_physics_object->pos = aabb_min_v4;
		new_physics_object->rot = Quatf::identity();
		new_physics_object->scale = Vec3f(span[0], span[1], span[2]);
	}
	else
	{
		//mw.ground_quad_raymesh->fromIndigoMesh(*mw.ground_quad_mesh);
		Reference<RayMesh> mesh = new RayMesh("raymesh", false);

		RayMesh::VertexVectorType& mesh_verts = mesh->getVertices();
		RayMesh::QuadVectorType& quads = mesh->getQuads();
		mesh_verts.resize(24);
		quads.resize(6);

		// Sides of parcel
		for(int i=0; i<4; ++i)
		{
			Vec2d v = this->verts[i];
			Vec2d next_v = this->verts[(i + 1) % 4];

			Vec3f v0((float)v.x, (float)v.y, (float)zbounds.x);
			Vec3f v1((float)next_v.x, (float)next_v.y, (float)zbounds.x);
			Vec3f v2((float)next_v.x, (float)next_v.y, (float)zbounds.y);
			Vec3f v3((float)v.x, (float)v.y, (float)zbounds.y);

			const int face = i;
			mesh_verts[face*4 + 0].pos = v0;
			mesh_verts[face*4 + 1].pos = v1;
			mesh_verts[face*4 + 2].pos = v2;
			mesh_verts[face*4 + 3].pos = v3;

			quads[face].vertex_indices[0] = face*4 + 0;
			quads[face].vertex_indices[1] = face*4 + 1;
			quads[face].vertex_indices[2] = face*4 + 2;
			quads[face].vertex_indices[3] = face*4 + 3;
			quads[face].setMatIndexAndUseShadingNormals(0, RayMesh_NoShadingNormals);
			for(int c=0; c<4; ++c)
				quads[face].uv_indices[c] = 0;

			
		}

		// Bottom
		{
			Vec3f v0((float)verts[0].x, (float)verts[0].y, (float)zbounds.x);
			Vec3f v1((float)verts[3].x, (float)verts[3].y, (float)zbounds.x);
			Vec3f v2((float)verts[2].x, (float)verts[2].y, (float)zbounds.x);
			Vec3f v3((float)verts[1].x, (float)verts[1].y, (float)zbounds.x);

			const int face = 4;
			mesh_verts[face*4 + 0].pos = v0;
			mesh_verts[face*4 + 1].pos = v1;
			mesh_verts[face*4 + 2].pos = v2;
			mesh_verts[face*4 + 3].pos = v3;

			quads[face].vertex_indices[0] = face*4 + 0;
			quads[face].vertex_indices[1] = face*4 + 1;
			quads[face].vertex_indices[2] = face*4 + 2;
			quads[face].vertex_indices[3] = face*4 + 3;
			quads[face].setMatIndexAndUseShadingNormals(0, RayMesh_NoShadingNormals);
			for(int c=0; c<4; ++c)
				quads[face].uv_indices[c] = 0;
		}

		// Top
		{
			Vec3f v0((float)verts[0].x, (float)verts[0].y, (float)zbounds.y);
			Vec3f v1((float)verts[1].x, (float)verts[1].y, (float)zbounds.y);
			Vec3f v2((float)verts[2].x, (float)verts[2].y, (float)zbounds.y);
			Vec3f v3((float)verts[3].x, (float)verts[3].y, (float)zbounds.y);

			const int face = 5;
			mesh_verts[face*4 + 0].pos = v0;
			mesh_verts[face*4 + 1].pos = v1;
			mesh_verts[face*4 + 2].pos = v2;
			mesh_verts[face*4 + 3].pos = v3;

			quads[face].vertex_indices[0] = face*4 + 0;
			quads[face].vertex_indices[1] = face*4 + 1;
			quads[face].vertex_indices[2] = face*4 + 2;
			quads[face].vertex_indices[3] = face*4 + 3;
			quads[face].setMatIndexAndUseShadingNormals(0, RayMesh_NoShadingNormals);
			for(int c=0; c<4; ++c)
				quads[face].uv_indices[c] = 0;
		}

		mesh->buildTrisFromQuads();

		new_physics_object->shape.jolt_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh->toIndigoMesh());

		new_physics_object->pos = Vec4f(0,0,0,1);
		new_physics_object->rot = Quatf::identity();
		new_physics_object->scale = Vec3f(1.f);
	}

	new_physics_object->userdata = this;
	new_physics_object->userdata_type = 1;
	return new_physics_object;
}


#endif // GUI_CLIENT


static const uint32 PARCEL_SERIALISATION_VERSION = 9;
/*
Version 3: added all_writeable.
Version 4: Added auction data
Version 5: Removed auction data, added parcel_auction_ids
Version 6: Added screenshot_ids (serialised to disk only, not over network)
Version 7: Added nft_status, minting_transaction_id (serialised to disk only, not over network)
Version 8: Added flags
Version 9: Added spawn_point
*/


static void writeToStreamCommon(const Parcel& parcel, OutStream& stream, bool writing_to_network_stream, uint32 peer_protocol_version)
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

	// Write all_writeable
	stream.writeUInt32(parcel.all_writeable ? 1 : 0);

	for(int i=0; i<4; ++i)
		writeToStream(parcel.verts[i], stream);
	writeToStream(parcel.zbounds, stream);

	// Write parcel_auction_ids 
	stream.writeUInt32((uint32)parcel.parcel_auction_ids.size());
	for(size_t i=0; i<parcel.parcel_auction_ids.size(); ++i)
		stream.writeUInt32(parcel.parcel_auction_ids[i]);

	if(writing_to_network_stream)
	{
		// Only write flags if the other end is using protocol version >= 32.
		if(peer_protocol_version >= 32) // flags were added in protocol version 32.
			stream.writeUInt32(parcel.flags);
	}
	else
		stream.writeUInt32(parcel.flags); // Always write flags to disk.

	if(writing_to_network_stream)
	{
		// Only write spawn_point if the other end is using protocol version >= 33.
		if(peer_protocol_version >= 33) // spawn_point was added in protocol version 33.
			writeToStream(parcel.spawn_point, stream);
	}
	else
		writeToStream(parcel.spawn_point, stream); // Always write spawn point to disk.
}


static void readFromStreamCommon(InStream& stream, uint32 version, Parcel& parcel, bool reading_network_stream, uint32 peer_protocol_version) // UID will have been read already
{
	parcel.owner_id = readUserIDFromStream(stream);
	parcel.created_time.readFromStream(stream);
	parcel.description = stream.readStringLengthFirst(10000);

	// Read admin_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("Too many admin_ids: " + toString(num));
		parcel.admin_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_ids[i] = readUserIDFromStream(stream);
	}

	// Read writer_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("Too many writer_ids: " + toString(num));
		parcel.writer_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_ids[i] = readUserIDFromStream(stream);
	}

	// Read child_parcel_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("Too many child_parcel_ids: " + toString(num));
		parcel.child_parcel_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.child_parcel_ids[i] = readParcelIDFromStream(stream);
	}

	// Read all_writeable
	if(version >= 3)
	{
		const uint32 val = stream.readUInt32();
		parcel.all_writeable = val != 0;
	}

	for(int i=0; i<4; ++i)
		parcel.verts[i] = readVec2FromStream<double>(stream);
	parcel.zbounds = readVec2FromStream<double>(stream);

	if(version >= 5)
	{
		// Read parcel_auction_ids
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("Too many parcel_auction_ids: " + toString(num));
		parcel.parcel_auction_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.parcel_auction_ids[i] = stream.readUInt32();
	}

	// Read flags
	if(reading_network_stream)
	{
		if(peer_protocol_version >= 32)
			parcel.flags = stream.readUInt32();
	}
	else
	{
		if(version >= 8)
			parcel.flags = stream.readUInt32();
	}

	// Read spawn_point
	if(reading_network_stream)
	{
		if(peer_protocol_version >= 33) // spawn_point was added in protocol version 33.
			parcel.spawn_point = readVec3FromStream<double>(stream);
	}
	else
	{
		if(version >= 9)
			parcel.spawn_point = readVec3FromStream<double>(stream);
	}

	parcel.build();
}


void writeToStream(const Parcel& parcel, OutStream& stream) // Write to file stream
{
	// Write version
	stream.writeUInt32(PARCEL_SERIALISATION_VERSION);

	writeToStreamCommon(parcel, stream, /*writing_to_network_stream=*/false, /*peer_protocol_version (not used)=*/0);

	// Write screenshot_ids (serialised to disk only, not sent over network)
	stream.writeUInt32((uint32)parcel.screenshot_ids.size());
	for(size_t i=0; i<parcel.screenshot_ids.size(); ++i)
		stream.writeUInt64(parcel.screenshot_ids[i]);

	// Write NFT/Eth stuff (serialised to disk only, not sent over network)
	stream.writeUInt32((uint32)parcel.nft_status);
	stream.writeUInt64((uint64)parcel.minting_transaction_id);
}


void readFromStream(InStream& stream, Parcel& parcel) // Read from file stream
{
	// Read version
	const uint32 version = stream.readUInt32();
	if(version > PARCEL_SERIALISATION_VERSION)
		throw glare::Exception("Parcel readFromStream: Unsupported version " + toString(version) + ", expected " + toString(PARCEL_SERIALISATION_VERSION) + ".");

	parcel.id = readParcelIDFromStream(stream);
	
	readFromStreamCommon(stream, version, parcel, /*reading_network_stream=*/false, /*peer_protocol_version (not used)=*/0);

	if(version >= 6)
	{
		// Read screenshot_ids (serialised to disk only)
		const uint32 num = stream.readUInt32();
		if(num > 10000)
			throw glare::Exception("Too many screenshot_ids: " + toString(num));
		parcel.screenshot_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.screenshot_ids[i] = stream.readUInt64();
	}

	if(version >= 7)
	{
		// Read NFT/Eth stuff (serialised to disk only, not sent over network)
		parcel.nft_status = (Parcel::NFTStatus)stream.readUInt32();
		parcel.minting_transaction_id = stream.readUInt64();
	}
}


void writeToNetworkStream(const Parcel& parcel, OutStream& stream, uint32 peer_protocol_version)
{
	writeToStreamCommon(parcel, stream, /*writing_to_network_stream=*/true, peer_protocol_version);

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


void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel, uint32 peer_protocol_version) // UID will have been read already
{
	readFromStreamCommon(stream, /*version=*/PARCEL_SERIALISATION_VERSION, parcel, /*reading_network_stream=*/true, peer_protocol_version);

	parcel.owner_name = stream.readStringLengthFirst(10000);

	// Read admin_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw glare::Exception("Too many admin_names: " + toString(num));
		parcel.admin_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}

	// Read writer_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw glare::Exception("Too many writer_names: " + toString(num));
		parcel.writer_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}

	parcel.build();
}


// restrict_changes: restrict changes to stuff clients are allowed to change.  Clients are not allowed to change bounds etc.
void Parcel::copyNetworkStateFrom(const Parcel& other, bool restrict_changes)
{
	const bool allow_all_changes = !restrict_changes;

	// NOTE: The data in here needs to match that in readFromNetworkStreamGivenUID()
	owner_id = other.owner_id;

	if(allow_all_changes)
		created_time = other.created_time;
	description = other.description;

	admin_ids = other.admin_ids;
	writer_ids = other.writer_ids;

	if(allow_all_changes)
		child_parcel_ids = other.child_parcel_ids;
	all_writeable = other.all_writeable;
	
	if(allow_all_changes)
	{
		for(int i=0; i<4; ++i)
			verts[i] = other.verts[i];
		zbounds = other.zbounds;
	}

	if(allow_all_changes)
		parcel_auction_ids = other.parcel_auction_ids;
	flags = other.flags;

	spawn_point = other.spawn_point;

	owner_name = other.owner_name;
	admin_names = other.admin_names;
	writer_names = other.writer_names;

	build();
}
