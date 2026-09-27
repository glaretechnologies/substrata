/*=====================================================================
LuaBuildScript.cpp
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LuaBuildScript.h"


#include "GUIClient.h"
#include "ClientThread.h"
#include "WorldState.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include "../shared/ResourceManager.h"
#include "../shared/LODGeneration.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"
#include <graphics/BatchedMesh.h>
#include <maths/mathstypes.h>
#include <maths/Matrix4f.h>
#include <lua/LuaVM.h>
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <utils/Reference.h>
#include <utils/Timer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/BitUtils.h>
#include <utils/Lock.h>
#include <utils/PlatformUtils.h>
#include <utils/RuntimeCheck.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/Exception.h>
#include <lualib.h>
#include <map>
#include <vector>


uint64 LuaBuilderState::makeToken()
{
	Lock lock(mutex);
	return next_token++;
}


bool LuaBuilderState::handleCreateObjectResponse(uint32 result, uint64 client_token, UID created_ob_uid, const std::string& error_msg)
{
	Lock lock(mutex);

	if(client_token == 0 || client_token >= next_token)
		return false; // Not a token we handed out, so this create came from somewhere else in the client.

	Outcome& outcome = outcomes[client_token];
	outcome.result = result;
	outcome.created_ob_uid = created_ob_uid;
	outcome.error_msg = error_msg;
	return true;
}


bool LuaBuilderState::getOutcomeForToken(uint64 client_token, Outcome& outcome_out) const
{
	Lock lock(mutex);

	auto res = outcomes.find(client_token);
	if(res == outcomes.end())
		return false;

	outcome_out = res->second;
	return true;
}


void LuaBuilderState::getOutcomesForRange(uint64 begin_token, uint64 end_token, size_t& num_responses_out, size_t& num_refused_out, std::string& first_error_out) const
{
	Lock lock(mutex);

	num_responses_out = 0;
	num_refused_out = 0;
	first_error_out.clear();

	for(auto it = outcomes.lower_bound(begin_token); it != outcomes.end() && it->first < end_token; ++it)
	{
		num_responses_out++;
		if(it->second.result != Protocol::CreateObjectResult_Success)
		{
			num_refused_out++;
			if(first_error_out.empty())
				first_error_out = it->second.error_msg;
		}
	}
}


void LuaBuilderState::forgetRange(uint64 begin_token, uint64 end_token)
{
	Lock lock(mutex);

	outcomes.erase(outcomes.lower_bound(begin_token), outcomes.lower_bound(end_token));
}


/*
Triangles a build script is assembling into one mesh.

Vertices are interleaved position, normal and UV floats, and are not shared between faces, so each face gets its own flat normal.
Triangles are kept in a list per material index, which become the mesh's batches.

UVs are in metres across the face rather than 0-1, so a texture tiles at the same scale whatever size the face is.
*/
class BuildMeshBuilder
{
public:
	BuildMeshBuilder() : cur_material_index(0) {}

	static const size_t MAX_NUM_VERTS = 500000;

	void addVert(const Vec4f& pos, const Vec4f& normal, float u, float v)
	{
		verts.push_back(pos[0]); verts.push_back(pos[1]); verts.push_back(pos[2]);
		verts.push_back(normal[0]); verts.push_back(normal[1]); verts.push_back(normal[2]);
		verts.push_back(u); verts.push_back(v);
	}

	size_t numVerts() const { return verts.size() / 8; }

	// v0..v3 wound counter-clockwise seen from the side the normal points towards.
	void addQuad(const Vec4f& v0, const Vec4f& v1, const Vec4f& v2, const Vec4f& v3)
	{
		if(numVerts() + 4 > MAX_NUM_VERTS)
			throw glare::Exception("Mesh has more than the maximum of " + toString(MAX_NUM_VERTS) + " vertices.");

		Vec4f normal = crossProduct(v1 - v0, v3 - v0);
		if(normal.length() < 1.0e-12f)
			throw glare::Exception("Quad is degenerate, so it has no normal.");
		normal = normalise(normal);

		// UV axes along the quad's own edges, in metres, so a texture keeps its scale on faces of different sizes.
		const float width  = (v1 - v0).length();
		const float height = (v3 - v0).length();

		const uint32 first = (uint32)numVerts();
		addVert(v0, normal, 0,     0);
		addVert(v1, normal, width, 0);
		addVert(v2, normal, width, height);
		addVert(v3, normal, 0,     height);

		js::Vector<uint32, 16>& indices = indices_for_material[cur_material_index];
		indices.push_back(first + 0); indices.push_back(first + 1); indices.push_back(first + 2);
		indices.push_back(first + 0); indices.push_back(first + 2); indices.push_back(first + 3);
	}

	// Appends triangles given as flat arrays.  'indices' are 0-based into the arrays passed in this call, and normals and uvs may
	// be empty.  With no normals, faces get flat normals, which means the triangles stop sharing vertices.
	void addTriangles(const std::vector<float>& positions, const std::vector<float>& normals, const std::vector<float>& uvs,
		const std::vector<uint32>& indices)
	{
		const size_t num_new_verts = positions.size() / 3;
		const bool have_normals = !normals.empty();
		const bool have_uvs = !uvs.empty();

		if(have_normals)
		{
			// Keep the vertices as given, with the index buffer offset past what's already in the mesh.
			if(numVerts() + num_new_verts > MAX_NUM_VERTS)
				throw glare::Exception("Mesh would have more than the maximum of " + toString(MAX_NUM_VERTS) + " vertices.");

			const uint32 first = (uint32)numVerts();
			for(size_t i=0; i<num_new_verts; ++i)
				addVert(
					Vec4f(positions[i*3 + 0], positions[i*3 + 1], positions[i*3 + 2], 1),
					Vec4f(normals  [i*3 + 0], normals  [i*3 + 1], normals  [i*3 + 2], 0),
					have_uvs ? uvs[i*2 + 0] : 0.f,
					have_uvs ? uvs[i*2 + 1] : 0.f);

			js::Vector<uint32, 16>& mesh_indices = indices_for_material[cur_material_index];
			for(size_t i=0; i<indices.size(); ++i)
				mesh_indices.push_back(first + indices[i]);
		}
		else
		{
			// One normal per triangle, so each triangle gets its own three vertices.
			if(numVerts() + indices.size() > MAX_NUM_VERTS)
				throw glare::Exception("Mesh would have more than the maximum of " + toString(MAX_NUM_VERTS) + " vertices.");

			js::Vector<uint32, 16>& mesh_indices = indices_for_material[cur_material_index];

			for(size_t t=0; t+2<indices.size(); t += 3)
			{
				const uint32 i0 = indices[t + 0], i1 = indices[t + 1], i2 = indices[t + 2];

				const Vec4f p0(positions[i0*3 + 0], positions[i0*3 + 1], positions[i0*3 + 2], 1);
				const Vec4f p1(positions[i1*3 + 0], positions[i1*3 + 1], positions[i1*3 + 2], 1);
				const Vec4f p2(positions[i2*3 + 0], positions[i2*3 + 1], positions[i2*3 + 2], 1);

				Vec4f normal = crossProduct(p1 - p0, p2 - p0);
				normal = (normal.length() < 1.0e-12f) ? Vec4f(0,0,1,0) : normalise(normal); // A degenerate triangle has no normal; it just won't be visible.

				const uint32 first = (uint32)numVerts();
				addVert(p0, normal, have_uvs ? uvs[i0*2 + 0] : 0.f, have_uvs ? uvs[i0*2 + 1] : 0.f);
				addVert(p1, normal, have_uvs ? uvs[i1*2 + 0] : 0.f, have_uvs ? uvs[i1*2 + 1] : 0.f);
				addVert(p2, normal, have_uvs ? uvs[i2*2 + 0] : 0.f, have_uvs ? uvs[i2*2 + 1] : 0.f);

				mesh_indices.push_back(first + 0); mesh_indices.push_back(first + 1); mesh_indices.push_back(first + 2);
			}
		}
	}


	// A box centred on 'centre' with the given full extents, rotated about 'axis' by 'angle'.
	void addBox(const Vec4f& centre, const Vec4f& size, const Vec4f& axis, float angle)
	{
		const Matrix4f rot = Matrix4f::rotationMatrix(normalise(axis), angle);

		const float hx = size[0] * 0.5f, hy = size[1] * 0.5f, hz = size[2] * 0.5f;

		// The eight corners, in the order -x-y-z, +x-y-z, +x+y-z, -x+y-z, then the same four at +z.
		Vec4f c[8];
		const float xs[8] = {-hx,  hx,  hx, -hx, -hx,  hx,  hx, -hx};
		const float ys[8] = {-hy, -hy,  hy,  hy, -hy, -hy,  hy,  hy};
		const float zs[8] = {-hz, -hz, -hz, -hz,  hz,  hz,  hz,  hz};
		for(int i=0; i<8; ++i)
			c[i] = centre + rot * Vec4f(xs[i], ys[i], zs[i], 0);

		addQuad(c[0], c[3], c[2], c[1]); // -z, wound so the normal points away from the box
		addQuad(c[4], c[5], c[6], c[7]); // +z
		addQuad(c[0], c[1], c[5], c[4]); // -y
		addQuad(c[2], c[3], c[7], c[6]); // +y
		addQuad(c[1], c[2], c[6], c[5]); // +x
		addQuad(c[3], c[0], c[4], c[7]); // -x
	}

	BatchedMeshRef build() const
	{
		if(verts.empty())
			throw glare::Exception("Mesh is empty: add something to it before uploading it.");

		BatchedMeshRef mesh = new BatchedMesh();

		// Normals are packed into 4 bytes: the server's mesh optimisation path only handles ComponentType_PackedNormal, and
		// rejects a mesh whose normals are plain floats.  Position 12 bytes + normal 4 + uv 8 = a 24 byte vertex.
		const size_t vert_size = 24;

		mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Position, BatchedMesh::ComponentType_Float,        /*offset=*/0));
		mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Normal,   BatchedMesh::ComponentType_PackedNormal, /*offset=*/12));
		mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0,     BatchedMesh::ComponentType_Float,        /*offset=*/16));

		const size_t num_verts = numVerts();
		mesh->vertex_data.resizeNoCopy(num_verts * vert_size);

		for(size_t v=0; v<num_verts; ++v)
		{
			const float* const src = &verts[v * 8];
			uint8* const dest = mesh->vertex_data.data() + v * vert_size;

			std::memcpy(dest, src, sizeof(float) * 3); // Position

			const uint32 packed_normal = batchedMeshPackNormal(Vec4f(src[3], src[4], src[5], 0));
			std::memcpy(dest + 12, &packed_normal, sizeof(uint32));

			std::memcpy(dest + 16, src + 6, sizeof(float) * 2); // UV
		}

		// One batch per material, with that material's triangles laid out contiguously.
		js::Vector<uint32, 16> all_indices;
		for(auto it = indices_for_material.begin(); it != indices_for_material.end(); ++it)
		{
			if(it->second.empty())
				continue;

			BatchedMesh::IndicesBatch batch;
			batch.indices_start = (uint32)all_indices.size();
			batch.num_indices   = (uint32)it->second.size();
			batch.material_index = it->first;
			mesh->batches.push_back(batch);

			for(size_t i=0; i<it->second.size(); ++i)
				all_indices.push_back(it->second[i]);
		}

		mesh->setIndexDataFromIndices(all_indices, numVerts());
		mesh->aabb_os = mesh->computeAABB();
		mesh->uv0_scale = 1.f;

		return mesh;
	}

	std::vector<float> verts; // 8 floats per vertex: position, normal, uv.
	std::map<uint32, js::Vector<uint32, 16> > indices_for_material;
	uint32 cur_material_index;
};


// What we need from a model resource in order to create an object referencing it.  Cached per run, since a build script typically
// makes many objects from a handful of models.
struct BuildModelInfo
{
	js::AABBox aabb_os;
	size_t num_verts;
	size_t num_tris;
};


/*
State for a single build-script run.  Stored in LuaScript::userdata, so the C functions registered for the script can reach it.

Per-object scripts store a LuaScriptEvaluator there instead, which is why the globals that expect one are not registered on a
build VM (see SubstrataLuaVMArgs::is_build_vm).
*/
class BuildScriptContext
{
public:
	BuildScriptContext() : gui_client(NULL), builder_state(NULL), first_create_token(0), end_create_token(0), max_output_size(0), output_truncated(false), max_objects(0), num_objects_created(0), num_objects_deleted(0) {}

	void appendOutput(const char* s, size_t len)
	{
		if(output.size() >= max_output_size)
		{
			output_truncated = true;
			return;
		}

		const size_t space_remaining = max_output_size - output.size();
		if(len > space_remaining)
		{
			output.append(s, space_remaining);
			output_truncated = true;
		}
		else
			output.append(s, len);
	}

	GUIClient* gui_client;
	LuaBuilderState* builder_state;

	// The tokens this run handed out, which form a contiguous range.  Used after the script finishes to see what the server made
	// of the objects.
	uint64 first_create_token;
	uint64 end_create_token;

	std::string output;
	size_t max_output_size;
	bool output_truncated;

	size_t max_objects;
	size_t num_objects_created;
	size_t num_objects_deleted;

	// Our own packet buffer: GUIClient::scratch_packet belongs to the main thread.
	SocketBufferOutStream packet;

	std::map<URLString, BuildModelInfo> model_info_cache;

	std::vector<BuildMeshBuilder> mesh_builders; // Meshes the script is assembling, indexed by the id in a mesh table.

	// A group the script has asked for with getOrCreateGroup().
	struct BuildGroup
	{
		BuildGroup() : needs_positioning(false) {}

		std::string name;
		UID uid; // invalidUID() until the group object has been made, which happens when the first object goes in the group.

		// Set while the group has nothing in it, so that the first object added to it decides where the group object goes.  A group the
		// run emptied counts as needing positioning even though the objects are still on their way out, which is why this is tracked
		// here rather than by looking for members in the world state.
		bool needs_positioning;
	};

	// The groups this run has asked for.  A script refers to a group by its 1-based index here rather than by UID: a group it has just
	// asked for has no UID yet, and one an earlier run made hasn't necessarily been sent back to us.
	std::vector<BuildGroup> groups;
};


static BuildScriptContext* getContext(lua_State* state)
{
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	return (BuildScriptContext*)script->userdata;
}


// print() for build scripts.  Follows glareLuaPrint in LuaVM.cpp, but appends to the run's output buffer, which is returned to the
// caller of the run_lua_build_script tool, and terminates each call with a newline so successive calls stay on separate lines.
static int build_print(lua_State* state)
{
	BuildScriptContext* context = getContext(state);

	const int num_args = lua_gettop(state);
	for(int i=1; i<=num_args; ++i)
	{
		size_t len;
		const char* s = luaL_tolstring(state, i, &len); // Convert to string using __tostring et al.
		if(i > 1)
			context->appendOutput("\t", 1);
		context->appendOutput(s, len);
		lua_pop(state, 1); // Pop the result of luaL_tolstring
	}
	context->appendOutput("\n", 1);

	return 0; // Number of results
}


static const URLString getCheckedURLField(lua_State* state, int table_index, const char* key)
{
	const std::string url = LuaUtils::getTableStringFieldWithEmptyDefault(state, table_index, key);
	if(url.size() > WorldObject::MAX_URL_SIZE)
		throw glare::Exception(std::string(key) + " is longer than the maximum of " + toString(WorldObject::MAX_URL_SIZE) + " characters.");
	return toURLString(url);
}


// A script refers to a group by its 1-based index in the run's group list, passed around as a plain Lua number, so a group is only
// meaningful to the run that asked for it.  Returns the index into BuildScriptContext::groups.
static size_t getGroupIndexFromHandle(BuildScriptContext* context, double val, const char* func_name)
{
	// !(val >= 1) rather than (val < 1) so NaN is caught too.
	if(!(val >= 1.0) || (val > (double)context->groups.size()) || ((double)(size_t)val != val))
		throw glare::Exception(std::string(func_name) + ": the group isn't one getOrCreateGroup() returned.");

	return (size_t)val - 1;
}


// Reads a material table.  The field names match the material fields per-object scripts use, so the two APIs stay consistent.
static WorldMaterialRef getTableWorldMaterial(lua_State* state, int table_index)
{
	WorldMaterialRef mat = new WorldMaterial();

	const Vec3f colour = LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "colour", Vec3f(0.85f));
	mat->colour_rgb = Colour3f(colour.x, colour.y, colour.z);
	mat->colour_texture_url = getCheckedURLField(state, table_index, "colour_texture_url");

	const Vec3f emission = LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "emission_rgb", Vec3f(0.f));
	mat->emission_rgb = Colour3f(emission.x, emission.y, emission.z);
	mat->emission_texture_url = getCheckedURLField(state, table_index, "emission_texture_url");

	mat->normal_map_url = getCheckedURLField(state, table_index, "normal_map_url");

	mat->roughness.val          = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "roughness_val", 0.5);
	mat->roughness.texture_url  = getCheckedURLField(state, table_index, "roughness_texture_url");
	mat->metallic_fraction.val  = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "metallic_fraction_val", 0.0);
	mat->opacity.val            = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "opacity_val", 1.0);

	mat->tex_matrix = LuaUtils::getTableMatrix2fFieldWithDefault(state, table_index, "tex_matrix", Matrix2f::identity());

	mat->emission_lum_flux_or_lum = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "emission_lum_flux_or_lum", 0.0);

	BitUtils::setOrZeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG,     LuaUtils::getTableBoolFieldWithDefault(state, table_index, "hologram",     false));
	BitUtils::setOrZeroBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG, LuaUtils::getTableBoolFieldWithDefault(state, table_index, "double_sided", false));

	return mat;
}


// Loads the model resource to get its object-space AABB, which the server needs in order to check the object fits in a parcel the
// user can write to.  The resource has to be present locally, which it is for a model the client has already displayed.
static const BuildModelInfo& getModelInfo(BuildScriptContext* context, const URLString& model_url)
{
	auto res = context->model_info_cache.find(model_url);
	if(res != context->model_info_cache.end())
		return res->second;

	if(!context->gui_client->resource_manager->isFileForURLPresent(model_url))
		throw glare::Exception("createObject(): this client does not have the model resource '" + toStdString(model_url) + "' on disk, so the "
			"object's bounds can't be computed.  Use a model that is loaded in the connected world.");

	BuildModelInfo info;
	try
	{
		const std::string model_path = context->gui_client->resource_manager->pathForURLForPresentResource(model_url);

		BatchedMeshRef mesh = LODGeneration::loadModel(model_path);

		info.aabb_os   = mesh->aabb_os;
		info.num_verts = mesh->numVerts();
		info.num_tris  = mesh->numIndices() / 3;
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("createObject(): failed to load model '" + toStdString(model_url) + "': " + e.what());
	}

	return context->model_info_cache.insert(std::make_pair(model_url, info)).first->second;
}


// Puts the group object of a group that has nothing in it at 'pos' - creating it there if the group is new, or moving it there if an
// earlier run made it - so a group's handle ends up on what the run built rather than wherever a previous run left it.
//
// Creating one waits for the server to assign the UID, which the objects going in the group need to refer to.  This is the only place a
// build script blocks on the server.
static void positionGroupObject(BuildScriptContext* context, size_t group_index, const Vec3d& pos)
{
	BuildScriptContext::BuildGroup& group = context->groups[group_index];

	GUIClient* gui_client = context->gui_client;

	if(group.uid.valid()) // If an earlier run made the group object, move it rather than making a second one.
	{
		// Keep the rest of the transform: the user may have rotated the handle to rotate the group.
		Vec3f axis(0,0,1);
		float angle = 0;
		Vec3f scale(1,1,1);
		{
			Lock lock(gui_client->world_state->mutex);

			auto res = gui_client->world_state->objects.find(group.uid);
			if(res == gui_client->world_state->objects.end())
				return; // The group object isn't here to be moved.  Leave it where it is rather than failing the build over it.

			const WorldObject* group_ob = res.getValue().ptr();
			axis  = group_ob->axis;
			angle = group_ob->angle;
			scale = group_ob->scale;
		}

		// The server checks separately that the user may move the object there.  If it refuses, the group keeps its old position, which
		// is no reason to fail the build.
		MessageUtils::initPacket(context->packet, Protocol::ObjectTransformUpdate);
		writeToStream(group.uid, context->packet);
		writeToStream(pos, context->packet);
		writeToStream(axis, context->packet);
		context->packet.writeFloat(angle);
		writeToStream(scale, context->packet);
		MessageUtils::updatePacketLengthField(context->packet);

		gui_client->client_thread->enqueueDataToSend(context->packet.buf);

		group.needs_positioning = false;
		return;
	}

	// Checked before sending, so a script that loops without bound stops here rather than on some later failure.
	if(context->num_objects_created >= context->max_objects)
		throw glare::Exception("createObject(): this script has reached the limit of " + toString(context->max_objects) + " objects.");

	// A group object has no mesh, so it gets a zero-size AABB at its position, and isn't collidable: it is a handle to grab, not
	// something to walk into.
	WorldObjectRef ob = new WorldObject();
	ob->object_type = WorldObject::ObjectType_Group;
	ob->content = group.name;
	ob->pos = pos;
	ob->axis = Vec3f(0,0,1);
	ob->angle = 0;
	ob->scale = Vec3f(1,1,1);
	ob->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1)));
	ob->setCollidable(false);
	ob->flags |= WorldObject::CREATED_VIA_MCP; // Keeps the client from selecting the group object as it arrives.

	// The server assigns the UID and echoes our token back, as for any other object the script creates.
	const uint64 create_token = context->builder_state->makeToken();
	ob->uid = UID(create_token);

	MessageUtils::initPacket(context->packet, Protocol::CreateObject);
	ob->writeToNetworkStream(context->packet);
	MessageUtils::updatePacketLengthField(context->packet);

	gui_client->client_thread->enqueueDataToSend(context->packet.buf);

	if(context->first_create_token == 0)
		context->first_create_token = create_token;
	context->end_create_token = create_token + 1;

	context->num_objects_created++;

	// Wait for the server's response: without the UID it assigned, the objects in the group have nothing to refer to.  The response is
	// handled on the main thread, which is not this one, hence the poll.
	const int timeout_s = 10;
	Timer wait_timer;
	LuaBuilderState::Outcome outcome;
	while(!context->builder_state->getOutcomeForToken(create_token, outcome))
	{
		if(wait_timer.elapsed() > (double)timeout_s)
			throw glare::Exception("createObject(): the server didn't answer within " + toString(timeout_s) + " s, so the group '" + group.name + "' wasn't made.");

		PlatformUtils::Sleep(10);
	}

	if(outcome.result != Protocol::CreateObjectResult_Success)
		throw glare::Exception("createObject(): the server refused to create the group object for '" + group.name + "': " + outcome.error_msg);
	if(!outcome.created_ob_uid.valid())
		throw glare::Exception("createObject(): the server accepted the group object for '" + group.name + "' without giving it a UID.");

	group.uid = outcome.created_ob_uid;
	group.needs_positioning = false;
}


static int createObject(lua_State* state)
{
	try
	{
		// Expected args:
		// Arg 1: ob_params : Table

		BuildScriptContext* context = getContext(state);

		if(!lua_istable(state, 1))
			throw glare::Exception("createObject(): arg 1 (ob_params) was not a table.");

		// Checked before the work below so a script that loops without bound stops here rather than on some later failure.
		if(context->num_objects_created >= context->max_objects)
			throw glare::Exception("createObject(): this script has reached the limit of " + toString(context->max_objects) + " objects.");

		const int table_index = 1;

		WorldObjectRef ob = new WorldObject();

		ob->model_url = getCheckedURLField(state, table_index, "model_url");
		if(ob->model_url.empty())
			throw glare::Exception("createObject(): 'model_url' is required.");

		ob->pos   = LuaUtils::getTableVec3dField(state, table_index, "pos");
		ob->axis  = LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "axis", Vec3f(0,0,1));
		ob->angle = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "angle", 0.0);
		ob->scale = LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "scale", Vec3f(1,1,1));

		checkTransformOK(ob->pos, ob->axis, ob->angle, ob->scale); // Throws on non-finite or degenerate values.

		ob->setCollidable(LuaUtils::getTableBoolFieldWithDefault(state, table_index, "collidable", ob->isCollidable()));
		ob->setDynamic   (LuaUtils::getTableBoolFieldWithDefault(state, table_index, "dynamic",    ob->isDynamic()));

		// Keeps the client from selecting each object as it arrives, which it does for objects the user has just made by hand.
		ob->flags |= WorldObject::CREATED_VIA_MCP;

		ob->content = LuaUtils::getTableStringFieldWithEmptyDefault(state, table_index, "content");
		if(ob->content.size() > WorldObject::MAX_CONTENT_SIZE)
			throw glare::Exception("createObject(): 'content' is longer than the maximum of " + toString(WorldObject::MAX_CONTENT_SIZE) + " characters.");

		ob->script = LuaUtils::getTableStringFieldWithEmptyDefault(state, table_index, "script");
		if(ob->script.size() > WorldObject::MAX_SCRIPT_SIZE)
			throw glare::Exception("createObject(): 'script' is longer than the maximum of " + toString(WorldObject::MAX_SCRIPT_SIZE) + " characters.");

		ob->target_url = getCheckedURLField(state, table_index, "target_url");

		// The group the object goes in, if any.  Read with lua_rawgetfield rather than a default value, so that a group_id that isn't a
		// group is an error instead of quietly making an object that belongs to nothing.  The group_id itself is set further down, since a
		// group that has nothing in it yet takes this object's position, and making its group object needs a connection.
		size_t group_index = std::numeric_limits<size_t>::max(); // max() if the object isn't going in a group.
		{
			const int group_value_type = lua_rawgetfield(state, table_index, "group_id"); // Pushes the field value onto the stack.
			if(group_value_type == LUA_TNIL)
				lua_pop(state, 1); // Pop the nil value
			else
			{
				if(group_value_type != LUA_TNUMBER)
				{
					lua_pop(state, 1); // Pop the group_id value
					throw glare::Exception("createObject(): 'group_id' must be the group that getOrCreateGroup() returned.");
				}

				const double group_val = lua_tonumber(state, -1);
				lua_pop(state, 1); // Pop the group_id value

				group_index = getGroupIndexFromHandle(context, group_val, "createObject()");
			}
		}

		// Materials
		const int value_type = lua_rawgetfield(state, table_index, "materials"); // Pushes the field value onto the stack.
		if(value_type == LUA_TTABLE)
		{
			const int max_num_mats = 100;
			for(int i=1; i<=max_num_mats; ++i)
			{
				runtimeCheck(lua_istable(state, -1)); // The materials table should still be on top of the stack

				const int mat_type = lua_rawgeti(state, /*table index=*/-1, i);
				if(mat_type == LUA_TTABLE)
				{
					ob->materials.push_back(getTableWorldMaterial(state, -1));

					lua_pop(state, 1); // Pop the material value
				}
				else
				{
					lua_pop(state, 1); // Pop the nil value
					break;
				}
			}
		}
		lua_pop(state, 1); // Pop the materials value

		// NOTE: GUIClient::createObject() calls setMaterialFlagsForObject() here.  That only inspects colour textures given as local
		// file paths, and build scripts give URLs, so there is nothing for it to do.

		// Checked after the arguments have been read, so a malformed call is reported as such whether or not there is a connection.
		if(!context->gui_client || context->gui_client->client_thread.isNull() || !context->builder_state)
			throw glare::Exception("createObject(): not connected to a server.");

		const BuildModelInfo& model_info = getModelInfo(context, ob->model_url);

		ob->setAABBOS(model_info.aabb_os);
		ob->max_model_lod_level = (model_info.num_verts <= 4 * 6) ? 0 : 2; // Don't generate LOD versions of a very small model.
		BitUtils::setOrZeroBit(ob->flags, WorldObject::MIN_MODEL_LOD_LEVEL_IS_NEGATIVE_1, model_info.num_tris > WorldObject::MIN_MODEL_LOD_LEVEL_NEG_1_TRI_THRESHOLD);

		// The group object of a group with nothing in it goes at this object's position.  Done after the checks above, so a call that fails
		// doesn't leave a group object behind at a position nothing ended up at.
		if(group_index != std::numeric_limits<size_t>::max())
		{
			if(context->groups[group_index].needs_positioning)
				positionGroupObject(context, group_index, ob->pos);

			ob->group_id = context->groups[group_index].uid;
		}

		// The server assigns the UID, creator and timestamps, and checks the user may create here.  The UID field of the message
		// instead carries a token the server echoes back, which is how we find out whether it accepted the object.
		// See Protocol::CreateObjectResponse.
		const uint64 create_token = context->builder_state->makeToken();
		ob->uid = UID(create_token);

		MessageUtils::initPacket(context->packet, Protocol::CreateObject);
		ob->writeToNetworkStream(context->packet);
		MessageUtils::updatePacketLengthField(context->packet);

		context->gui_client->client_thread->enqueueDataToSend(context->packet.buf);

		if(context->first_create_token == 0)
			context->first_create_token = create_token;
		context->end_create_token = create_token + 1;

		context->num_objects_created++;

		return 0; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


static const size_t MAX_MESHES_PER_RUN = 64;


// The mesh a method was called on.  Mesh tables hold an index into the run's builders rather than a pointer, so the builders can
// move as the vector grows.
static BuildMeshBuilder& getMeshBuilderForSelf(lua_State* state, BuildScriptContext* context)
{
	const double id = LuaUtils::getTableNumberField(state, /*table index=*/1, "id");
	if(!(id >= 0) || ((size_t)id >= context->mesh_builders.size()))
		throw glare::Exception("Not a mesh, or a mesh from a previous run.");
	return context->mesh_builders[(size_t)id];
}


static const Vec4f getPointArg(lua_State* state, int index)
{
	const Vec3f v = LuaUtils::getVec3f(state, index);
	return Vec4f(v.x, v.y, v.z, 1);
}


// mesh:addBox(centre, size [, axis, angle])
static int mesh_addBox(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);
		BuildMeshBuilder& builder = getMeshBuilderForSelf(state, context);

		const Vec4f centre = getPointArg(state, 2);
		const Vec3f size = LuaUtils::getVec3f(state, 3);

		Vec4f axis(0, 0, 1, 0);
		float angle = 0;
		if(lua_gettop(state) >= 4)
		{
			const Vec3f axis_v = LuaUtils::getVec3f(state, 4);
			if(axis_v.length() < 1.0e-9f)
				throw glare::Exception("addBox(): axis is zero-length.");
			axis = Vec4f(axis_v.x, axis_v.y, axis_v.z, 0);
			angle = (float)LuaUtils::getDoubleArg(state, 5);
		}

		builder.addBox(centre, Vec4f(size.x, size.y, size.z, 0), axis, angle);

		return 0; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// Reads the flat array of numbers in the table at 'table_index' into 'floats_out'.  'name' is what error messages call the array.
static void readNumberArray(lua_State* state, int table_index, const char* name, size_t max_num, std::vector<float>& floats_out)
{
	table_index = lua_absindex(state, table_index); // So the index stays valid as elements are pushed.

	const size_t num = (size_t)lua_objlen(state, table_index);
	if(num > max_num)
		throw glare::Exception(std::string(name) + " has " + toString(num) + " entries, more than the maximum of " + toString(max_num) + ".");

	floats_out.resize(num);
	for(size_t i=0; i<num; ++i)
	{
		const int elem_type = lua_rawgeti(state, table_index, (int)(i + 1)); // Lua arrays are 1-based.
		if(elem_type != LUA_TNUMBER)
		{
			lua_pop(state, 1);
			throw glare::Exception(std::string(name) + " has a value at position " + toString(i + 1) + " that is not a number.");
		}
		floats_out[i] = (float)lua_tonumber(state, -1);
		lua_pop(state, 1);
	}
}


// Reads a field holding a flat array of numbers into 'floats_out'.  An absent field gives an empty array.
static void readNumberArrayField(lua_State* state, int table_index, const char* key, size_t max_num, std::vector<float>& floats_out)
{
	floats_out.clear();

	const int value_type = lua_rawgetfield(state, table_index, key); // Pushes the field value onto the stack.
	if(value_type == LUA_TNIL)
	{
		lua_pop(state, 1);
		return;
	}
	if(value_type != LUA_TTABLE)
	{
		lua_pop(state, 1);
		throw glare::Exception(std::string(key) + " must be an array of numbers.");
	}

	readNumberArray(state, /*table index=*/-1, key, max_num, floats_out);

	lua_pop(state, 1); // Pop the field value
}


/*
mesh:addTriangles{ positions = {...}, normals = {...}, uvs = {...}, indices = {...} }

The bulk form: give it the whole geometry at once rather than a call per face.  positions is required, three numbers per vertex;
normals is optional, three per vertex; uvs is optional, two per vertex; indices is optional and 1-based, three per triangle.
*/
static int mesh_addTriangles(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);
		BuildMeshBuilder& builder = getMeshBuilderForSelf(state, context);

		if(!lua_istable(state, 2))
			throw glare::Exception("addTriangles(): expected a table of arrays.");

		const size_t max_floats = BuildMeshBuilder::MAX_NUM_VERTS * 3;

		std::vector<float> positions, normals, uvs, index_floats;
		readNumberArrayField(state, 2, "positions", max_floats, positions);
		readNumberArrayField(state, 2, "normals",   max_floats, normals);
		readNumberArrayField(state, 2, "uvs",       max_floats, uvs);
		readNumberArrayField(state, 2, "indices",   max_floats, index_floats);

		if(positions.empty())
			throw glare::Exception("addTriangles(): 'positions' is required.");
		if((positions.size() % 3) != 0)
			throw glare::Exception("addTriangles(): 'positions' has " + toString(positions.size()) + " numbers, which is not a whole number of x,y,z triples.");

		const size_t num_verts = positions.size() / 3;

		if(!normals.empty() && (normals.size() != positions.size()))
			throw glare::Exception("addTriangles(): 'normals' has " + toString(normals.size() / 3) + " entries but 'positions' has " + toString(num_verts) + ".");
		if(!uvs.empty() && (uvs.size() != num_verts * 2))
			throw glare::Exception("addTriangles(): 'uvs' has " + toString(uvs.size() / 2) + " entries but 'positions' has " + toString(num_verts) + ".");

		// Without indices the vertices are taken in threes as triangles.
		std::vector<uint32> indices;
		if(index_floats.empty())
		{
			if((num_verts % 3) != 0)
				throw glare::Exception("addTriangles(): with no 'indices', 'positions' must hold whole triangles, but it has " + toString(num_verts) + " vertices.");

			indices.resize(num_verts);
			for(size_t i=0; i<num_verts; ++i)
				indices[i] = (uint32)i;
		}
		else
		{
			if((index_floats.size() % 3) != 0)
				throw glare::Exception("addTriangles(): 'indices' has " + toString(index_floats.size()) + " entries, which is not a whole number of triangles.");

			indices.resize(index_floats.size());
			for(size_t i=0; i<index_floats.size(); ++i)
			{
				const float index = index_floats[i];
				if(!(index >= 1) || (index > (float)num_verts))
					throw glare::Exception("addTriangles(): index " + toString(index) + " at position " + toString(i + 1) + " is out of range: indices are 1-based, "
						"so they run from 1 to " + toString(num_verts) + " here.");

				indices[i] = (uint32)index - 1;
			}
		}

		builder.addTriangles(positions, normals, uvs, indices);

		return 0; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// mesh:addQuad(v0, v1, v2, v3), wound counter-clockwise seen from the visible side.
static int mesh_addQuad(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);
		BuildMeshBuilder& builder = getMeshBuilderForSelf(state, context);

		builder.addQuad(getPointArg(state, 2), getPointArg(state, 3), getPointArg(state, 4), getPointArg(state, 5));

		return 0; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// mesh:setMaterial(i) - subsequent geometry uses the i'th material of the object the mesh ends up on.  1-based, like the
// materials array passed to createObject.
static int mesh_setMaterial(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);
		BuildMeshBuilder& builder = getMeshBuilderForSelf(state, context);

		const double mat_index = LuaUtils::getDoubleArg(state, 2);
		if(!(mat_index >= 1) || (mat_index > 100))
			throw glare::Exception("setMaterial(): material index must be between 1 and 100.");

		builder.cur_material_index = (uint32)mat_index - 1;

		return 0; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// mesh:upload([name]) - writes the mesh out and returns a model_url to pass to createObject.
static int mesh_upload(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);
		BuildMeshBuilder& builder = getMeshBuilderForSelf(state, context);

		std::string name = "build_mesh";
		if(lua_gettop(state) >= 2)
			name = LuaUtils::getStringArg(state, 2);

		if(!context->gui_client)
			throw glare::Exception("upload(): not connected to a server.");

		BatchedMeshRef mesh = builder.build();

		const URLString mesh_url = context->gui_client->writeMeshToResourceDirGetURL(mesh, name);

		// Remember what createObject needs, so it doesn't read back the file we just wrote.
		BuildModelInfo info;
		info.aabb_os   = mesh->aabb_os;
		info.num_verts = mesh->numVerts();
		info.num_tris  = mesh->numIndices() / 3;
		context->model_info_cache[mesh_url] = info;

		LuaUtils::pushString(state, toStdString(mesh_url));
		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// createMesh() - returns a mesh to add geometry to, for building one object out of many pieces rather than many objects.
static int createMesh(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);

		if(context->mesh_builders.size() >= MAX_MESHES_PER_RUN)
			throw glare::Exception("This script has made the maximum of " + toString(MAX_MESHES_PER_RUN) + " meshes.");

		const size_t id = context->mesh_builders.size();
		context->mesh_builders.push_back(BuildMeshBuilder());

		lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/6);
		LuaUtils::setNumberAsTableField(state, "id", (double)id);
		LuaUtils::setCFunctionAsTableField(state, mesh_addBox,      /*debugname=*/"mesh_addBox",      /*key=*/"addBox");
		LuaUtils::setCFunctionAsTableField(state, mesh_addQuad,     /*debugname=*/"mesh_addQuad",     /*key=*/"addQuad");
		LuaUtils::setCFunctionAsTableField(state, mesh_addTriangles, /*debugname=*/"mesh_addTriangles", /*key=*/"addTriangles");
		LuaUtils::setCFunctionAsTableField(state, mesh_setMaterial, /*debugname=*/"mesh_setMaterial", /*key=*/"setMaterial");
		LuaUtils::setCFunctionAsTableField(state, mesh_upload,      /*debugname=*/"mesh_upload",      /*key=*/"upload");

		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


static const size_t MAX_GROUPS_PER_RUN = 64;


// getOrCreateGroup(name) - returns the group with this name, to pass as the group_id field of createObject().  A build script groups
// what it makes so that the next run of it can clear the lot with deleteObjectsInGroup().
//
// A group is a group object: a mesh-less object whose content field holds the name and whose UID identifies the group.  Its position is
// the handle the user grabs to move the group about, and is set from the first object put in the group, so this call takes no position
// and creates nothing on the server: see positionGroupObject().
static int getOrCreateGroup(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);

		const std::string name = LuaUtils::getStringArg(state, /*index=*/1);
		if(name.empty())
			throw glare::Exception("getOrCreateGroup(): the group name can't be empty.");
		if(name.size() > WorldObject::MAX_CONTENT_SIZE)
			throw glare::Exception("getOrCreateGroup(): the group name is longer than the maximum of " + toString(WorldObject::MAX_CONTENT_SIZE) + " characters.");

		// Checked after the arguments have been read, so a malformed call is reported as such whether or not there is a connection.
		if(!context->gui_client || context->gui_client->client_thread.isNull() || context->gui_client->world_state.isNull() || !context->builder_state)
			throw glare::Exception("getOrCreateGroup(): not connected to a server.");

		GUIClient* gui_client = context->gui_client;

		// A group this run has already asked for, which the world state can't answer for: its group object may not have been made yet, and
		// if it has, the server hasn't necessarily sent it back to us.
		for(size_t i=0; i<context->groups.size(); ++i)
			if(context->groups[i].name == name)
			{
				lua_pushnumber(state, (double)(i + 1)); // 1-based, as arrays are in Lua.
				return 1; // Number of results
			}

		if(context->groups.size() >= MAX_GROUPS_PER_RUN)
			throw glare::Exception("getOrCreateGroup(): this script has asked for the maximum of " + toString(MAX_GROUPS_PER_RUN) + " groups.");

		BuildScriptContext::BuildGroup group;
		group.name = name;

		// Look for a group of this user's with this name, from an earlier run.  Groups are matched per-user, so two users' "dock" groups are
		// different groups.  Lowest UID wins, so a name that has somehow ended up on two group objects gives the same answer each run.
		{
			Lock lock(gui_client->world_state->mutex);

			for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
			{
				const WorldObject* ob = it.getValue().ptr();
				if((ob->object_type == WorldObject::ObjectType_Group) && (ob->creator_id == gui_client->logged_in_user_id) && (ob->content == name))
					if(ob->uid < group.uid)
						group.uid = ob->uid;
			}
		}

		// A group that didn't exist has nothing in it, so the first object added to it decides where its group object goes.
		group.needs_positioning = !group.uid.valid();

		context->groups.push_back(group);

		lua_pushnumber(state, (double)context->groups.size()); // 1-based, as arrays are in Lua.
		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// deleteObjectsInGroup(group) - destroys the objects in a group and returns how many.  A build script calls this at the start of a run
// to remove what the previous run made.  The group object itself is left, since it is what the next run finds the group by.
//
// Members that are themselves groups go too, along with their own members, however deep that runs: destroying a sub-group's object
// without its members would leave those members in the world with nothing left referring to them.
//
// Restricted to objects this user created, so scripts run by different users don't clear each other's work.
//
// Only finds objects the client has been sent, which for the world around the camera is all of them.
static int deleteObjectsInGroup(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);

		const size_t group_index = getGroupIndexFromHandle(context, LuaUtils::getDoubleArg(state, /*index=*/1), "deleteObjectsInGroup()");

		if(!context->gui_client || context->gui_client->client_thread.isNull() || context->gui_client->world_state.isNull())
			throw glare::Exception("deleteObjectsInGroup(): not connected to a server.");

		GUIClient* gui_client = context->gui_client;

		// The group is emptied, so the next object put in it decides where its group object goes.  Noted here rather than looked up later,
		// because the objects destroyed below are still in the world state until the server has dealt with the messages sent at the end.
		context->groups[group_index].needs_positioning = true;

		const UID group_uid = context->groups[group_index].uid;
		if(!group_uid.valid())
		{
			lua_pushnumber(state, 0.0); // The group has no group object yet, so nothing can be in it.
			return 1; // Number of results
		}

		// Collect the UIDs under the lock, then send the messages without holding it.
		std::vector<UID> uids;
		{
			Lock lock(gui_client->world_state->mutex);

			// A scan per group, taking in each group found on the way.  Builds nest a handful of groups at most, so a scan each is fine.
			std::vector<UID> groups_to_scan(1, group_uid);

			for(size_t g=0; g<groups_to_scan.size(); ++g) // NOTE: groups_to_scan grows as sub-groups are found.
			{
				const UID cur_group_uid = groups_to_scan[g];
				runtimeCheck(cur_group_uid.valid());

				for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
				{
					const WorldObject* ob = it.getValue().ptr();
					if((ob->group_id == cur_group_uid) && (ob->creator_id == gui_client->logged_in_user_id))
					{
						uids.push_back(ob->uid);

						if(ob->object_type == WorldObject::ObjectType_Group)
						{
							// Scan a group only once, or a cycle of groups would send this round forever.  The group we started from is
							// already in the list, so a group that is somehow a member of itself is covered as well.
							bool already_listed = false;
							for(size_t i=0; i<groups_to_scan.size(); ++i)
								if(groups_to_scan[i] == ob->uid)
									already_listed = true;

							if(!already_listed)
								groups_to_scan.push_back(ob->uid);
						}
					}
				}
			}
		}

		for(size_t i=0; i<uids.size(); ++i)
		{
			MessageUtils::initPacket(context->packet, Protocol::DestroyObject);
			writeToStream(uids[i], context->packet);
			MessageUtils::updatePacketLengthField(context->packet);

			gui_client->client_thread->enqueueDataToSend(context->packet.buf);
		}

		context->num_objects_deleted += uids.size();

		lua_pushnumber(state, (double)uids.size());
		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


static const size_t MAX_TERRAIN_QUERY_POINTS = 100000;


// Gets the terrain heights at 'points' from the main thread, which owns the terrain, and waits for the answer.  Each call waits for the
// main thread to get to the message, which is why getTerrainHeights() exists to ask about many points at once.
static void queryTerrainHeights(BuildScriptContext* context, const std::vector<Vec2f>& points, const char* func_name, std::vector<float>& heights_out)
{
	if(!context->gui_client)
		throw glare::Exception(std::string(func_name) + ": not connected to a server.");

	TerrainHeightQueryRef query = new TerrainHeightQuery();
	query->points = points;

	context->gui_client->msg_queue.enqueue(new TerrainQueriesToProcessMessage(query));

	const double timeout_s = 10;
	Timer wait_timer;

	Lock lock(query->mutex);
	while(!query->done)
	{
		const double remaining_s = timeout_s - wait_timer.elapsed();
		if(remaining_s <= 0)
			throw glare::Exception(std::string(func_name) + ": the client didn't answer within " + toString((int)timeout_s) + " s.");

		query->condition.waitWithTimeout(query->mutex, remaining_s);
	}

	if(!query->error_msg.empty())
		throw glare::Exception(std::string(func_name) + ": " + query->error_msg);

	heights_out = query->heights;
}


// getTerrainHeight(x, y) - the height of the terrain surface at (x, y).
static int getTerrainHeight(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);

		const double x = LuaUtils::getDoubleArg(state, 1);
		const double y = LuaUtils::getDoubleArg(state, 2);

		const std::vector<Vec2f> points(1, Vec2f((float)x, (float)y));
		std::vector<float> heights;
		queryTerrainHeights(context, points, "getTerrainHeight()", heights);

		lua_pushnumber(state, heights[0]);
		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// getTerrainHeights{x1, y1, x2, y2, ...} - the terrain heights at many points, as {z1, z2, ...}.  A flat array like the ones addTriangles
// takes.
static int getTerrainHeights(lua_State* state)
{
	try
	{
		BuildScriptContext* context = getContext(state);

		if(!lua_istable(state, 1))
			throw glare::Exception("getTerrainHeights(): expected an array of numbers, x1, y1, x2, y2, ...");

		std::vector<float> xy;
		readNumberArray(state, /*table index=*/1, "getTerrainHeights()", MAX_TERRAIN_QUERY_POINTS * 2, xy);
		if((xy.size() % 2) != 0)
			throw glare::Exception("getTerrainHeights(): the array has " + toString(xy.size()) + " numbers, which is not a whole number of x, y pairs.");

		std::vector<Vec2f> points(xy.size() / 2);
		for(size_t i=0; i<points.size(); ++i)
			points[i] = Vec2f(xy[i*2 + 0], xy[i*2 + 1]);

		std::vector<float> heights;
		if(!points.empty())
			queryTerrainHeights(context, points, "getTerrainHeights()", heights);

		lua_createtable(state, /*num array elems=*/(int)heights.size(), /*num non-array elems=*/0);
		for(size_t i=0; i<heights.size(); ++i)
		{
			lua_pushnumber(state, heights[i]);
			lua_rawseti(state, /*table index=*/-2, (int)(i + 1)); // Lua arrays are 1-based.
		}
		return 1; // Number of results
	}
	catch(glare::Exception& e)
	{
		luaL_error(state, "%s", e.what().c_str()); // Throws a lua_exception, with the script location of the call put in front of the message.
	}
}


// Build scripts print via build_print, so this handler just catches output from anything else that writes through the script output
// handler.  It must not assume LuaScript::userdata is a LuaScriptEvaluator, unlike GUIClient's handler.
class BuildScriptOutputHandler : public LuaScriptOutputHandler
{
public:
	BuildScriptOutputHandler(BuildScriptContext* context_) : context(context_) {}

	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override
	{
		context->appendOutput(s, len);
	}

	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override
	{
		context->appendOutput(msg.data(), msg.size());
		context->appendOutput("\n", 1);
	}

	BuildScriptContext* context;
};


void LuaBuildScript::run(GUIClient* gui_client, LuaBuilderState* builder_state, const std::string& script_src, const LuaBuildScriptOptions& options, LuaBuildScriptResults& results_out)
{
	Timer timer;

	BuildScriptContext context;
	context.gui_client = gui_client;
	context.builder_state = builder_state;
	context.max_output_size = options.max_output_size;
	context.max_objects = options.max_objects;

	BuildScriptOutputHandler output_handler(&context);

	try
	{
		SubstrataLuaVM::SubstrataLuaVMArgs vm_args;
		vm_args.gui_client = gui_client;
		vm_args.player_physics = gui_client ? &gui_client->player_physics : NULL;
		vm_args.is_build_vm = true;

		// Replace print at the VM level rather than through LuaScriptOptions::c_funcs: those are set on a script's thread after it
		// has been loaded, by which point the script has already resolved print to the built-in one.
		vm_args.vm_c_funcs.push_back(LuaCFunction(build_print, "print"));

		Reference<SubstrataLuaVM> substrata_lua_vm = new SubstrataLuaVM(vm_args);

		LuaScriptOptions script_options;
		script_options.max_num_interrupts = options.max_num_interrupts;
		script_options.chunkname = options.script_name;
		script_options.script_output_handler = &output_handler;
		script_options.userdata = &context;

		// A new global name, so registering it on the script's thread works.  Only replacing an existing one has to go through the VM.
		script_options.c_funcs.push_back(LuaCFunction(createObject, "createObject"));
		script_options.c_funcs.push_back(LuaCFunction(getOrCreateGroup, "getOrCreateGroup"));
		script_options.c_funcs.push_back(LuaCFunction(deleteObjectsInGroup, "deleteObjectsInGroup"));
		script_options.c_funcs.push_back(LuaCFunction(createMesh, "createMesh"));
		script_options.c_funcs.push_back(LuaCFunction(getTerrainHeight, "getTerrainHeight"));
		script_options.c_funcs.push_back(LuaCFunction(getTerrainHeights, "getTerrainHeights"));

		// NOTE: declared after substrata_lua_vm so it is destroyed first: the script holds a thread on the VM's lua_State.
		LuaScript script(substrata_lua_vm->lua_vm.ptr(), script_options, script_src);

		script.exec();

		results_out.success = true;
	}
	catch(LuaScriptExcepWithLocation& e)
	{
		results_out.success = false;
		results_out.error_msg = e.messageWithLocations();
	}
	catch(glare::Exception& e)
	{
		results_out.success = false;
		results_out.error_msg = e.what();
	}

	// Wait a little for the server to say what it did with the objects.  The responses come back on the main thread well after the
	// script has finished, and an object the script counted as created can still have been refused, so without this the caller has
	// no way to tell a successful build from one that silently built nothing.
	if(builder_state && (context.end_create_token > context.first_create_token))
	{
		const size_t num_sent = (size_t)(context.end_create_token - context.first_create_token);
		const double wait_timeout_s = myMin(2.0 + num_sent * 0.002, 15.0);

		Timer wait_timer;
		while(1)
		{
			builder_state->getOutcomesForRange(context.first_create_token, context.end_create_token,
				results_out.num_creates_answered, results_out.num_creates_refused, results_out.first_refusal_msg);

			if((results_out.num_creates_answered >= num_sent) || (wait_timer.elapsed() > wait_timeout_s))
				break;

			PlatformUtils::Sleep(10);
		}

		builder_state->forgetRange(context.first_create_token, context.end_create_token);
	}

	results_out.output = context.output;
	results_out.output_truncated = context.output_truncated;
	results_out.num_objects_created = context.num_objects_created;
	results_out.num_objects_deleted = context.num_objects_deleted;
	results_out.elapsed_s = timer.elapsed();
}


#if BUILD_TESTS


#include <utils/TestUtils.h>
#include <utils/TestExceptionUtils.h>


void LuaBuildScript::test()
{
	conPrint("LuaBuildScript::test()");

	// Test the mesh builder directly.
	{
		BuildMeshBuilder builder;
		builder.addBox(Vec4f(0,0,0,1), Vec4f(2,4,6,0), Vec4f(0,0,1,0), /*angle=*/0);

		testAssert(builder.numVerts() == 24); // Six faces, four verts each, not shared, so each face gets a flat normal.

		BatchedMeshRef mesh = builder.build();
		testAssert(mesh->numVerts() == 24);
		testAssert(mesh->numIndices() == 36); // 12 triangles
		testAssert(mesh->batches.size() == 1);
		testAssert(mesh->batches[0].material_index == 0);

		// Normals have to be packed: the server's mesh optimisation only handles ComponentType_PackedNormal, and a mesh with
		// float normals is rejected once it gets there.
		testAssert(mesh->getAttribute(BatchedMesh::VertAttribute_Normal).component_type == BatchedMesh::ComponentType_PackedNormal);
		testAssert(mesh->vertexSize() == 24); // Position 12 + packed normal 4 + uv 8

		// Unpacking a vertex's normal should give back the axis-aligned normal that went in.
		{
			const size_t normal_offset = mesh->getAttribute(BatchedMesh::VertAttribute_Normal).offset_B;
			uint32 packed_normal;
			std::memcpy(&packed_normal, mesh->vertex_data.data() + normal_offset, sizeof(uint32));

			const Vec4f n = batchedMeshUnpackNormal(packed_normal);
			testAssert(epsEqual(n.length(), 1.f));
		}

		testAssert(epsEqual(mesh->aabb_os.min_[0], -1.f) && epsEqual(mesh->aabb_os.max_[0], 1.f));
		testAssert(epsEqual(mesh->aabb_os.min_[1], -2.f) && epsEqual(mesh->aabb_os.max_[1], 2.f));
		testAssert(epsEqual(mesh->aabb_os.min_[2], -3.f) && epsEqual(mesh->aabb_os.max_[2], 3.f));

		// Every box face should have a normal pointing away from the centre, i.e. agreeing with the direction to the face.
		const size_t vert_size_floats = 8;
		for(size_t v=0; v<builder.numVerts(); ++v)
		{
			const Vec4f pos   (builder.verts[v*vert_size_floats + 0], builder.verts[v*vert_size_floats + 1], builder.verts[v*vert_size_floats + 2], 0);
			const Vec4f normal(builder.verts[v*vert_size_floats + 3], builder.verts[v*vert_size_floats + 4], builder.verts[v*vert_size_floats + 5], 0);
			testAssert(dot(pos, normal) > 0); // Outward-facing
		}
	}

	// Test that geometry is grouped into one batch per material.
	{
		BuildMeshBuilder builder;
		builder.addBox(Vec4f(0,0,0,1), Vec4f(1,1,1,0), Vec4f(0,0,1,0), 0);
		builder.cur_material_index = 2;
		builder.addBox(Vec4f(4,0,0,1), Vec4f(1,1,1,0), Vec4f(0,0,1,0), 0);

		BatchedMeshRef mesh = builder.build();
		testAssert(mesh->batches.size() == 2);
		testAssert(mesh->batches[0].material_index == 0);
		testAssert(mesh->batches[1].material_index == 2);
		testAssert(mesh->batches[0].num_indices == 36);
		testAssert(mesh->batches[1].num_indices == 36);
		testAssert(mesh->batches[1].indices_start == 36); // Laid out after the first batch
	}

	// Test the bulk form: one triangle given as flat arrays, with supplied normals so the vertices are kept as given.
	{
		BuildMeshBuilder builder;
		const std::vector<float> positions = {0,0,0,  1,0,0,  0,1,0};
		const std::vector<float> normals   = {0,0,1,  0,0,1,  0,0,1};
		const std::vector<float> uvs       = {0,0,  1,0,  0,1};
		const std::vector<uint32> indices  = {0, 1, 2};

		builder.addTriangles(positions, normals, uvs, indices);
		testAssert(builder.numVerts() == 3);

		BatchedMeshRef mesh = builder.build();
		testAssert(mesh->numIndices() == 3);
	}

	// Test that with no normals, shared vertices are split so each triangle gets a flat normal.
	{
		BuildMeshBuilder builder;
		const std::vector<float> positions = {0,0,0,  1,0,0,  1,1,0,  0,1,0}; // A quad as two triangles sharing two vertices
		const std::vector<uint32> indices  = {0,1,2,  0,2,3};

		builder.addTriangles(positions, std::vector<float>(), std::vector<float>(), indices);
		testAssert(builder.numVerts() == 6); // Split: 2 triangles x 3, not the 4 shared vertices given

		// Both triangles face +z, since they were wound counter-clockwise in the z=0 plane.
		for(size_t v=0; v<builder.numVerts(); ++v)
			testAssert(epsEqual(builder.verts[v*8 + 5], 1.f));
	}

	// Test that a second addTriangles call offsets its indices past the geometry already in the mesh.
	{
		BuildMeshBuilder builder;
		const std::vector<float> positions = {0,0,0,  1,0,0,  0,1,0};
		const std::vector<float> normals   = {0,0,1,  0,0,1,  0,0,1};
		const std::vector<uint32> indices  = {0, 1, 2};

		builder.addTriangles(positions, normals, std::vector<float>(), indices);
		builder.addTriangles(positions, normals, std::vector<float>(), indices);

		testAssert(builder.numVerts() == 6);
		testAssert(builder.indices_for_material[0].size() == 6);
		testAssert(builder.indices_for_material[0][3] == 3); // Second call's first index, offset past the first three verts
	}

	// Test that an empty mesh is rejected rather than producing a mesh with no vertices.
	{
		BuildMeshBuilder builder;
		testThrowsExcepContainingString([&]() { builder.build(); }, "empty");
	}

	// Test LuaBuilderState, which pairs up the server's create responses with the run that asked for the objects.
	{
		LuaBuilderState state;

		const uint64 tok_a = state.makeToken();
		const uint64 tok_b = state.makeToken();
		const uint64 tok_c = state.makeToken();
		testAssert(tok_a != 0); // Token 0 is never handed out.
		testAssert((tok_b == tok_a + 1) && (tok_c == tok_b + 1)); // A run's tokens are contiguous.

		testAssert(!state.handleCreateObjectResponse(Protocol::CreateObjectResult_Success, /*token=*/0, UID(100), "")); // Not one of ours
		testAssert(!state.handleCreateObjectResponse(Protocol::CreateObjectResult_Success, tok_c + 1, UID(100), "")); // Never handed out

		testAssert(state.handleCreateObjectResponse(Protocol::CreateObjectResult_Success, tok_a, UID(100), ""));
		testAssert(state.handleCreateObjectResponse(Protocol::CreateObjectResult_NoPermission, tok_b, UID::invalidUID(), "no permission here"));

		// getOrCreateGroup() polls for a single token, and needs the UID the server assigned.
		LuaBuilderState::Outcome outcome;
		testAssert(state.getOutcomeForToken(tok_a, outcome));
		testAssert((outcome.result == Protocol::CreateObjectResult_Success) && (outcome.created_ob_uid == UID(100)));
		testAssert(!state.getOutcomeForToken(tok_c, outcome)); // Not answered yet.

		size_t num_responses, num_refused;
		std::string first_error;

		// tok_c hasn't been answered yet, so only two of the three are counted.
		state.getOutcomesForRange(tok_a, tok_c + 1, num_responses, num_refused, first_error);
		testAssert(num_responses == 2);
		testAssert(num_refused == 1);
		testStringsEqual(first_error, "no permission here");

		// A range covering only the successful token reports no refusals.
		state.getOutcomesForRange(tok_a, tok_b, num_responses, num_refused, first_error);
		testAssert((num_responses == 1) && (num_refused == 0) && first_error.empty());

		state.forgetRange(tok_a, tok_c + 1);
		state.getOutcomesForRange(tok_a, tok_c + 1, num_responses, num_refused, first_error);
		testAssert(num_responses == 0);
	}

	// Test that a script runs and its output is captured.  gui_client is null: these scripts don't call anything that needs it.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "print('hello') print('a', 1)", LuaBuildScriptOptions(), results);

		testAssert(results.success);
		testAssert(results.error_msg.empty());
		testStringsEqual(results.output, "hello\na\t1\n");
	}

	// Test that a compile error is reported with its location.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "print('a'\nthis is not lua", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(!results.error_msg.empty());
		testAssert(::hasPrefix(results.error_msg, "Line "));
	}

	// Test that a named script gets its name in compile errors, in file:line:col form.
	{
		LuaBuildScriptOptions options;
		options.script_name = "C:/builds/island.lua";

		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "print('a'\nthis is not lua", options, results);

		testAssert(!results.success);
		testAssert(::hasPrefix(results.error_msg, "C:/builds/island.lua:"));
	}

	// Test that a runtime error is reported.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "error('boom')", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(!results.error_msg.empty());
	}

	// Test that an error thrown by one of our functions reports the script line that called it.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "local x = 1\ncreateObject({ pos = Vec3d(0,0,0) })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, ":2: createObject(): 'model_url' is required"));
	}

	// The same for a mesh method, in a named script: a zero-height box has degenerate faces.
	{
		LuaBuildScriptOptions options;
		options.script_name = "C:/builds/island.lua";

		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "local m = createMesh()\n\nm:addBox(Vec3f(0,0,0), Vec3f(1,1,0))", options, results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "C:/builds/island.lua:3: Quad is degenerate"));
	}

	// Test that a build script can catch an error from one of the build functions with pcall, since they raise errors with luaL_error.
	// Per-object script functions throw glare::Exception instead, which pcall doesn't catch.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "local ok, msg = pcall(createObject, 42)\nprint(ok, msg)", LuaBuildScriptOptions(), results);

		testAssert(results.success);
		testAssert(::hasPrefix(results.output, "false\t"));
		testAssert(StringUtils::containsString(results.output, "was not a table"));
	}

	// Test that the globals that need a LuaScriptEvaluator are absent, rather than present and reading a BuildScriptContext as one.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createTimer(1.0, false)", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
	}

	// Test that the execution budget aborts a long-running script.
	{
		LuaBuildScriptOptions options;
		options.max_num_interrupts = 1000;

		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "local x = 0 while true do x = x + 1 end", options, results);

		testAssert(!results.success);
	}

	// Test that createObject reports the lack of a connection rather than dereferencing a null client.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0) })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(results.num_objects_created == 0);
		testAssert(StringUtils::containsString(results.error_msg, "not connected"));
	}

	// Test that a non-table argument is rejected.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject(42)", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "was not a table"));
	}

	// Test that a missing model_url is rejected.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ pos = Vec3d(0,0,0) })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "'model_url' is required"));
	}

	// Test that a missing pos is rejected.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh' })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
	}

	// Test that passing a Vec3d where a Vec3f is wanted is an error rather than silently using the default.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0), axis = Vec3d(1,0,0) })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "Vec3f"));
	}

	// Test that a group_id that isn't a group is rejected, rather than making an object that belongs to nothing.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0), group_id = -1 })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "getOrCreateGroup"));

		LuaBuildScriptResults results2;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0), group_id = 1.5 })", LuaBuildScriptOptions(), results2);

		testAssert(!results2.success);
		testAssert(StringUtils::containsString(results2.error_msg, "getOrCreateGroup"));

		// A group name in place of the group: the mistake a script is most likely to make.
		LuaBuildScriptResults results3;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0), group_id = 'dock' })", LuaBuildScriptOptions(), results3);

		testAssert(!results3.success);
		testAssert(StringUtils::containsString(results3.error_msg, "'group_id' must be"));
	}

	// Test that a group the run never asked for is rejected: a group is an index into the run's groups, so any value is out of range
	// until getOrCreateGroup() has returned one.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = 'a.bmesh', pos = Vec3d(0,0,0), group_id = 1 })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "getOrCreateGroup"));
	}

	// Test that getOrCreateGroup checks its argument before needing a connection.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "getOrCreateGroup('')", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "can't be empty"));

		LuaBuildScriptResults results2;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "getOrCreateGroup('dock')", LuaBuildScriptOptions(), results2);

		testAssert(!results2.success);
		testAssert(StringUtils::containsString(results2.error_msg, "not connected")); // Got past argument parsing.
	}

	// Test that deleteObjectsInGroup checks its argument before needing a connection.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "deleteObjectsInGroup(-1)", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "getOrCreateGroup"));

		LuaBuildScriptResults results2;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "deleteObjectsInGroup('dock')", LuaBuildScriptOptions(), results2);

		testAssert(!results2.success);
		testAssert(StringUtils::containsString(results2.error_msg, "not a number"));
	}

	// Test that an over-long URL is rejected.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({ model_url = string.rep('a', 2000), pos = Vec3d(0,0,0) })", LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "model_url is longer than"));
	}

	// Test that a well-formed call gets as far as needing a connection, i.e. the arguments above are all accepted.
	{
		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL,
			"createObject({ model_url = 'a.bmesh', pos = Vec3d(1,2,3), axis = Vec3f(0,0,1), angle = 1.5, scale = Vec3f(2,2,2),\n"
			"  materials = { { colour = Vec3f(1,0,0), roughness_val = 0.2, double_sided = true }, { colour = Vec3f(0,1,0) } } })",
			LuaBuildScriptOptions(), results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "not connected"));
	}

	// Test that the object limit is checked before anything else, so a runaway loop stops on the limit.
	{
		LuaBuildScriptOptions options;
		options.max_objects = 0;

		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "createObject({})", options, results);

		testAssert(!results.success);
		testAssert(StringUtils::containsString(results.error_msg, "limit of 0 objects"));
	}

	// Test that output past max_output_size is dropped.
	{
		LuaBuildScriptOptions options;
		options.max_output_size = 16;

		LuaBuildScriptResults results;
		LuaBuildScript::run(/*gui_client=*/NULL, /*builder_state=*/NULL, "for i=1, 100 do print('0123456789') end", options, results);

		testAssert(results.success);
		testAssert(results.output.size() <= 16);
		testAssert(results.output_truncated);
	}

	conPrint("LuaBuildScript::test() done.");
}


#endif // BUILD_TESTS
