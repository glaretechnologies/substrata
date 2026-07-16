/*=====================================================================
MCPHandlers.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPHandlers.h"


#include "RequestInfo.h"
#include "ResponseUtils.h"
#include "Escaping.h"
#include "../server/ServerWorldState.h"
#include "../server/ObjectPermissions.h"
#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include "../shared/Avatar.h"
#include "../shared/WorldDetails.h"
#include "../shared/WorldStateLock.h"
#include "../shared/LODChunk.h"
#include "../shared/RateLimiter.h"
#include <JSONParser.h>
#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <TimeStamp.h>
#include <BitUtils.h>
#include <Clock.h>
#include <cmath>


namespace MCPHandlers
{


// The protocol version we advertise.  See https://modelcontextprotocol.io/
static const char* MCP_PROTOCOL_VERSION = "2024-11-05";

// JSON-RPC 2.0 error codes:
static const int JSONRPC_PARSE_ERROR      = -32700;
static const int JSONRPC_INVALID_REQUEST  = -32600;
static const int JSONRPC_METHOD_NOT_FOUND = -32601;
static const int JSONRPC_INVALID_PARAMS   = -32602;
static const int JSONRPC_INTERNAL_ERROR   = -32603;


// The JSON-RPC request 'id', which must be echoed back in the response.  May be a number, a string, or absent (for notifications).
struct RequestID
{
	enum Type { Type_None, Type_Number, Type_String };
	RequestID() : type(Type_None), num_val(0) {}
	Type type;
	double num_val;
	std::string str_val;
};


static const std::string idToJSON(const RequestID& id)
{
	if(id.type == RequestID::Type_String)
		return "\"" + web::Escaping::JSONEscape(id.str_val) + "\"";
	else if(id.type == RequestID::Type_Number)
	{
		if(id.num_val == (double)(int64)id.num_val) // If integral, print as an integer:
			return toString((int64)id.num_val);
		else
			return doubleToString(id.num_val);
	}
	else
		return "null";
}


static void writeJSONResponse(web::ReplyInfo& reply_info, const std::string& json)
{
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, json.data(), json.size(), /*content type=*/"application/json");
}


static void writeResult(web::ReplyInfo& reply_info, const RequestID& id, const std::string& result_json)
{
	writeJSONResponse(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + idToJSON(id) + ",\"result\":" + result_json + "}");
}


static void writeError(web::ReplyInfo& reply_info, const RequestID& id, int code, const std::string& message)
{
	writeJSONResponse(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + idToJSON(id) + ",\"error\":{\"code\":" + toString(code) +
		",\"message\":\"" + web::Escaping::JSONEscape(message) + "\"}}");
}


// Write an HTTP 429 Too Many Requests response, with a Retry-After header so clients know when to retry.
static void writeHTTP429Response(web::ReplyInfo& reply_info, const std::string& body, int retry_after_s)
{
	const std::string response =
		"HTTP/1.1 429 Too Many Requests\r\n"
		"Content-Type: text/plain\r\n"
		"Retry-After: " + toString(retry_after_s) + "\r\n"
		"Content-Length: " + toString(body.size()) + "\r\n"
		"\r\n" + body;

	web::ResponseUtils::writeRawString(reply_info, response);
}


// Rate-limiting parameters for the MCP endpoint, applied per authenticated user (see handleMCPRequest).
static const double MCP_RATE_LIMIT_PERIOD_S        = 60.0; // Sliding-window length, in seconds.
static const size_t MCP_RATE_LIMIT_MAX_IN_PERIOD   = 120;  // Max requests allowed per user within the window.

// Rate-limiting parameters for failed authentication attempts on the MCP endpoint, applied per client IP address, so the
// endpoint can't be used as a password or API-key guessing oracle (see handleMCPRequest).  A legitimate client should produce
// almost no auth failures, so the budget is much smaller than the per-user request budget above.
static const double MCP_FAILED_AUTH_RATE_LIMIT_PERIOD_S        = 60.0; // Sliding-window length, in seconds.
static const size_t MCP_FAILED_AUTH_RATE_LIMIT_MAX_IN_PERIOD   = 10;   // Max failed auth attempts allowed per IP within the window.

// Cap on the number of per-IP failed-auth rate limiters, to bound memory use when an attacker connects from many IPs.
static const size_t MAX_NUM_FAILED_AUTH_RATE_LIMITERS          = 10000;


// Record a failed authentication attempt from the given client IP, creating the rate limiter for the IP lazily.
static void recordFailedAuthAttempt(ServerAllWorldsState& world_state, const std::string& client_ip, WorldStateLock& /*lock*/)
{
	if(!world_state.server_config.do_mcp_rate_limiting)
		return;

	RateLimiter* rate_limiter;
	const auto res = world_state.mcp_failed_auth_rate_limiters.find(client_ip);
	if(res == world_state.mcp_failed_auth_rate_limiters.end())
	{
		// Bound the number of per-IP rate limiters.  Clearing forgets recent failures, but bounds memory use.
		if(world_state.mcp_failed_auth_rate_limiters.size() >= MAX_NUM_FAILED_AUTH_RATE_LIMITERS)
			world_state.mcp_failed_auth_rate_limiters.clear();

		rate_limiter = new RateLimiter(MCP_FAILED_AUTH_RATE_LIMIT_PERIOD_S, MCP_FAILED_AUTH_RATE_LIMIT_MAX_IN_PERIOD);
		world_state.mcp_failed_auth_rate_limiters.insert(std::make_pair(client_ip, Reference<RateLimiter>(rate_limiter)));
	}
	else
		rate_limiter = res->second.ptr();

	rate_limiter->checkAddEvent(Clock::getCurTimeRealSec());
}


// Build the result object for a tools/call response (an MCP CallToolResult).
static const std::string makeToolResult(const std::string& text, bool is_error)
{
	return "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(text) + "\"}],\"isError\":" + (is_error ? "true" : "false") + "}";
}


//===================== JSON output helpers =====================


static const std::string vec3ToJSON(const Vec3d& v)
{
	return "{\"x\":" + doubleToString(v.x) + ",\"y\":" + doubleToString(v.y) + ",\"z\":" + doubleToString(v.z) + "}";
}


// Returns the (non-null) world state for the given world name, throwing glare::Exception if there is no such world.
// The returned pointer is owned by all_worlds and is valid as long as the world state lock is held.
static ServerWorldState* getWorld(ServerAllWorldsState& all_worlds, const std::string& world_name, WorldStateLock& lock)
{
	auto res = all_worlds.world_states.find(world_name);
	if(res == all_worlds.world_states.end())
		throw glare::Exception("No world with name '" + world_name + "'");
	return res->second.ptr();
}


//===================== Tools =====================


static const std::string tool_listWorlds(ServerAllWorldsState& all_worlds)
{
	WorldStateLock lock(all_worlds.mutex);

	std::string s = "[";
	bool first = true;
	for(auto it = all_worlds.world_states.begin(); it != all_worlds.world_states.end(); ++it)
	{
		ServerWorldState* world = it->second.ptr();
		if(!first)
			s += ",";
		first = false;
		s += "{\"name\":\"" + web::Escaping::JSONEscape(it->first) + "\"" +
			",\"description\":\"" + web::Escaping::JSONEscape(world->details.description) + "\"" +
			",\"num_objects\":" + toString(world->getObjects(lock).size()) +
			",\"num_avatars\":" + toString(world->getAvatars(lock).size()) +
			",\"num_parcels\":" + toString(world->getParcels(lock).size()) + "}";
	}
	s += "]";
	return s;
}


static const std::string tool_getWorldInfo(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args)
{
	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	return "{\"name\":\"" + web::Escaping::JSONEscape(world_name) + "\"" +
		",\"description\":\"" + web::Escaping::JSONEscape(world->details.description) + "\"" +
		",\"num_objects\":" + toString(world->getObjects(lock).size()) +
		",\"num_avatars\":" + toString(world->getAvatars(lock).size()) +
		",\"num_parcels\":" + toString(world->getParcels(lock).size()) + "}";
}


static const std::string tool_listAvatars(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args)
{
	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	std::string s = "[";
	bool first = true;
	for(auto it = world->getAvatars(lock).begin(); it != world->getAvatars(lock).end(); ++it)
	{
		const Avatar* av = it->second.ptr();
		if(!first)
			s += ",";
		first = false;
		s += "{\"uid\":" + toString(av->uid.value()) +
			",\"name\":\"" + web::Escaping::JSONEscape(av->name) + "\"" +
			",\"pos\":" + vec3ToJSON(av->pos) + "}";
	}
	s += "]";
	return s;
}


static const std::string tool_listObjectsNear(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args)
{
	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");
	const Vec3d centre(args.getChildDoubleValue(parser, "x"), args.getChildDoubleValue(parser, "y"), args.getChildDoubleValue(parser, "z"));
	const double radius = args.getChildDoubleValue(parser, "radius");
	const size_t limit = (size_t)args.getChildDoubleValueWithDefaultVal(parser, "limit", /*default=*/50);
	const double radius2 = radius * radius;

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	std::string s = "[";
	bool first = true;
	size_t num_added = 0;
	for(auto it = world->getObjects(lock).begin(); (it != world->getObjects(lock).end()) && (num_added < limit); ++it)
	{
		const WorldObject* ob = it->second.ptr();
		if(ob->pos.getDist2(centre) <= radius2)
		{
			if(!first)
				s += ",";
			first = false;
			s += ob->serialiseToJSON();
			num_added++;
		}
	}
	s += "]";
	return s;
}


static const std::string tool_getObject(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args)
{
	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");
	const UID uid((uint64)args.getChildDoubleValue(parser, "uid"));

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	auto res = world->getObjects(lock).find(uid);
	if(res == world->getObjects(lock).end())
		throw glare::Exception("No object with UID " + toString(uid.value()) + " in world '" + world_name + "'");

	return res->second->serialiseToJSON();
}


//===================== Mutation tools =====================

// World-mutation tools act as the user that owns the API key used to authenticate the request (see handleMCPRequest),
// and are subject to that user's object/parcel permissions.  God users pass all permission checks (see isGodUser()).

static const float MCP_CHUNK_W = 128;

// Object-count limits for MCP-created objects, to stop an agent from filling up a world or parcel.  God users are exempt.
static const size_t MCP_MAX_OBJECTS_PER_PARCEL = 1000;
static const size_t MCP_MAX_OBJECTS_PER_WORLD  = 10000; // Applies to non-main worlds (e.g. personal/private worlds).


// Mark the LOD chunk containing the object as needing a rebuild, so the merged chunk mesh is regenerated.
// Uses the object position for the chunk lookup (an approximation of the centroid).
static void markLODChunkNeedsRebuild(ServerWorldState* world, const WorldObject* ob, WorldStateLock& lock)
{
	if(!BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH))
	{
		const int chunk_x = (int)std::floor(ob->pos.x / MCP_CHUNK_W);
		const int chunk_y = (int)std::floor(ob->pos.y / MCP_CHUNK_W);
		const Vec3i chunk_coords(chunk_x, chunk_y, 0);

		auto res = world->getLODChunks(lock).find(chunk_coords);
		if(res != world->getLODChunks(lock).end())
			res->second->needs_rebuild = true;
	}
}


// Enforce object-count limits on MCP-created objects, to stop an agent from filling up a world or parcel.  God users are
// exempt (consistent with permission checks).  Throws glare::Exception if a limit would be exceeded.
// NOTE: the world state mutex must be held.
static void checkObjectCountLimits(const WorldObject& ob, const std::string& world_name, ServerWorldState& world, const UserID acting_user_id, WorldStateLock& lock)
{
	if(isGodUser(acting_user_id))
		return;

	// Per-world limit, applied to non-main worlds (e.g. personal/private worlds).  The main world (name "") is not capped
	// here - creation there is constrained by the per-parcel limit below.
	if(!world_name.empty() && (world.getObjects(lock).size() >= MCP_MAX_OBJECTS_PER_WORLD))
		throw glare::Exception("World object limit reached (" + toString(MCP_MAX_OBJECTS_PER_WORLD) + " objects); cannot create more objects in this world.");

	// Per-parcel limit: if the new object lies within a parcel, cap the number of (live) objects in that parcel.  There is
	// no per-parcel object index, so we count by scanning the world's objects - acceptable at the (rate-limited) frequency
	// of MCP object creation.
	const Vec4f ob_pos = ob.pos.toVec4fPoint();
	const Parcel* target_parcel = NULL;
	for(auto it = world.getParcels(lock).begin(); it != world.getParcels(lock).end(); ++it)
		if(it->second->pointInParcel(ob_pos))
		{
			target_parcel = it->second.ptr();
			break;
		}

	if(target_parcel)
	{
		size_t num_in_parcel = 0;
		for(auto it = world.getObjects(lock).begin(); it != world.getObjects(lock).end(); ++it)
		{
			const WorldObject* other = it->second.ptr();
			if((other->state != WorldObject::State_Dead) && target_parcel->pointInParcel(other->pos.toVec4fPoint()))
				num_in_parcel++;
		}

		if(num_in_parcel >= MCP_MAX_OBJECTS_PER_PARCEL)
			throw glare::Exception("Parcel object limit reached (" + toString(MCP_MAX_OBJECTS_PER_PARCEL) + " objects); cannot create more objects in this parcel.");
	}
}


// Insert a fully-constructed object (object_type, model_url, content, pos, axis, angle, scale, materials all set)
// into the given world as the acting user, after checking creation permissions.  Returns the new object's UID as JSON.
static const std::string createObjectInWorld(ServerAllWorldsState& all_worlds, const std::string& world_name, WorldObjectRef ob,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	// TODO: need to set object-space AABB

	if(all_worlds.isInReadOnlyMode())
		throw glare::Exception("Server is in read-only mode; cannot create objects.");

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	if(!userHasObjectCreationPermissions(*ob, acting_user_id, *world, lock))
		throw glare::Exception("Permission denied: cannot create an object at this position.");

	checkObjectCountLimits(*ob, world_name, *world, acting_user_id, lock);

	ob->creator_id = acting_user_id;
	ob->created_time = TimeStamp::currentTime();
	ob->last_modified_time = ob->created_time;
	ob->creator_name = acting_user_name;
	ob->uid = all_worlds.getNextObjectUID();
	ob->state = WorldObject::State_JustCreated;
	ob->from_remote_other_dirty = true;
	world->addWorldObjectAsDBDirty(ob, lock);
	world->getDirtyFromRemoteObjects(lock).insert(ob);
	world->getObjects(lock).insert(std::make_pair(ob->uid, ob));

	markLODChunkNeedsRebuild(world, ob.ptr(), lock);
	all_worlds.markAsChanged();

	return "{\"uid\":" + toString(ob->uid.value()) + "}";
}


static const std::string tool_createObject(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	WorldObjectRef ob = new WorldObject();
	ob->flags |= WorldObject::CREATED_VIA_MCP;
	ob->object_type = WorldObject::objectTypeForString(args.getChildStringValueWithDefaultVal(parser, "object_type", /*default=*/"generic"));
	ob->model_url = toURLString(args.getChildStringValueWithDefaultVal(parser, "model_url", /*default=*/""));
	ob->content = args.getChildStringValueWithDefaultVal(parser, "content", /*default=*/"");
	ob->pos = Vec3d(args.getChildDoubleValue(parser, "x"), args.getChildDoubleValue(parser, "y"), args.getChildDoubleValue(parser, "z"));
	ob->axis = Vec3f(0, 0, 1);
	ob->angle = 0;
	ob->scale = Vec3f((float)args.getChildDoubleValueWithDefaultVal(parser, "scale_x", 1.0), (float)args.getChildDoubleValueWithDefaultVal(parser, "scale_y", 1.0), (float)args.getChildDoubleValueWithDefaultVal(parser, "scale_z", 1.0));

	return createObjectInWorld(all_worlds, args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/""), ob, acting_user_id, acting_user_name);
}


//===================== Primitive-creation tools =====================

// Built-in primitive meshes (present on the server as resources).  Dimensions (object-space AABB):
// Cube:       [-0.5, 0.5]^3           (unit cube, so scale = size in metres)
// Cylinder:   xy in [-0.25, 0.25], z in [-0.5, 0.5]  (radius 0.25, height 1, z-aligned)
// Icosahedron:[-0.5, 0.5]^3           (used for spheres; radius 0.5)
static const char* MCP_CUBE_MESH_URL     = "Cube_obj_12971581758459554602.bmesh";
static const char* MCP_CYLINDER_MESH_URL = "Cylinder_obj_8542616007088785005.bmesh";
static const char* MCP_SPHERE_MESH_URL   = "Icosahedron_obj_17649497764207890525.bmesh";


static const Vec3d readVec3(const JSONParser& parser, const JSONNode& v)
{
	return Vec3d(v.getChildDoubleValue(parser, "x"), v.getChildDoubleValue(parser, "y"), v.getChildDoubleValue(parser, "z"));
}


// Reads either "pos" (the centre) or "base_pos" (the centre of the bottom face); exactly one must be present.
// half_height is the distance from the centre to the bottom face, used to convert base_pos to a centre.
static const Vec3d getPrimitiveCentre(const JSONParser& parser, const JSONNode& args, double half_height)
{
	const bool has_pos = args.hasChild("pos");
	const bool has_base = args.hasChild("base_pos");
	if(has_pos == has_base)
		throw glare::Exception("Provide exactly one of 'pos' or 'base_pos'.");

	if(has_pos)
		return readVec3(parser, args.getChildObject(parser, "pos"));
	else
	{
		Vec3d centre = readVec3(parser, args.getChildObject(parser, "base_pos"));
		centre.z += half_height;
		return centre;
	}
}


// Builds a primitive object with the given mesh, centre and scale, reading axis/angle and an optional single material from args.
static WorldObjectRef makePrimitiveObject(const char* mesh_url, const Vec3d& centre, const Vec3f& scale, const JSONParser& parser, const JSONNode& args)
{
	WorldObjectRef ob = new WorldObject();
	ob->flags |= WorldObject::CREATED_VIA_MCP;
	ob->object_type = WorldObject::ObjectType_Generic;
	ob->model_url = toURLString(mesh_url);
	ob->pos = centre;
	ob->scale = scale;
	ob->axis = Vec3f(
		(float)args.getChildDoubleValueWithDefaultVal(parser, "axis_x", 0.0),
		(float)args.getChildDoubleValueWithDefaultVal(parser, "axis_y", 0.0),
		(float)args.getChildDoubleValueWithDefaultVal(parser, "axis_z", 1.0));
	ob->angle = (float)args.getChildDoubleValueWithDefaultVal(parser, "angle", 0.0);

	if(args.hasChild("material"))
		ob->materials.push_back(WorldMaterial::fromJSON(parser, args.getChildObject(parser, "material")));
	else
		ob->materials.push_back(new WorldMaterial()); // Default material.

	return ob;
}


static const std::string tool_createCube(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	const Vec3f scale(
		(float)args.getChildDoubleValueWithDefaultVal(parser, "size_x", 1.0),
		(float)args.getChildDoubleValueWithDefaultVal(parser, "size_y", 1.0),
		(float)args.getChildDoubleValueWithDefaultVal(parser, "size_z", 1.0));
	const Vec3d centre = getPrimitiveCentre(parser, args, /*half_height=*/scale.z * 0.5);
	WorldObjectRef ob = makePrimitiveObject(MCP_CUBE_MESH_URL, centre, scale, parser, args);
	return createObjectInWorld(all_worlds, args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/""), ob, acting_user_id, acting_user_name);
}


static const std::string tool_createCylinder(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	const double radius = args.getChildDoubleValueWithDefaultVal(parser, "radius", 0.5);
	const double height = args.getChildDoubleValueWithDefaultVal(parser, "height", 1.0);
	const Vec3f scale((float)(4.0 * radius), (float)(4.0 * radius), (float)height); // Cylinder mesh has radius 0.25 and height 1.
	const Vec3d centre = getPrimitiveCentre(parser, args, /*half_height=*/height * 0.5);
	WorldObjectRef ob = makePrimitiveObject(MCP_CYLINDER_MESH_URL, centre, scale, parser, args);
	return createObjectInWorld(all_worlds, args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/""), ob, acting_user_id, acting_user_name);
}


static const std::string tool_createSphere(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	const double radius = args.getChildDoubleValueWithDefaultVal(parser, "radius", 0.5);
	const Vec3f scale((float)(2.0 * radius), (float)(2.0 * radius), (float)(2.0 * radius)); // Icosahedron mesh has radius 0.5.
	const Vec3d centre = getPrimitiveCentre(parser, args, /*half_height=*/radius);
	WorldObjectRef ob = makePrimitiveObject(MCP_SPHERE_MESH_URL, centre, scale, parser, args);
	return createObjectInWorld(all_worlds, args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/""), ob, acting_user_id, acting_user_name);
}


static const std::string tool_editObject(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args,
	const UserID acting_user_id, const std::string& acting_user_name)
{
	if(all_worlds.isInReadOnlyMode())
		throw glare::Exception("Server is in read-only mode; cannot edit objects.");

	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");
	const UID uid((uint64)args.getChildDoubleValue(parser, "uid"));

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	auto res = world->getObjects(lock).find(uid);
	if(res == world->getObjects(lock).end())
		throw glare::Exception("No object with UID " + toString(uid.value()) + " in world '" + world_name + "'");
	WorldObject* ob = res->second.ptr();

	if(!userHasObjectWritePermissions(*ob, acting_user_id, acting_user_name, *world, /*allow_light_mapper_bot_full_perms=*/false, lock))
		throw glare::Exception("Permission denied: cannot modify object " + toString(uid.value()) + ".");

	// Apply any provided transform fields, leaving others unchanged.
	if(args.hasChild("x") && args.hasChild("y") && args.hasChild("z"))
		ob->pos = Vec3d(args.getChildDoubleValue(parser, "x"), args.getChildDoubleValue(parser, "y"), args.getChildDoubleValue(parser, "z"));
	if(args.hasChild("scale_x") && args.hasChild("scale_y") && args.hasChild("scale_z"))
		ob->scale = Vec3f((float)args.getChildDoubleValue(parser, "scale_x"), (float)args.getChildDoubleValue(parser, "scale_y"), (float)args.getChildDoubleValue(parser, "scale_z"));
	if(args.hasChild("axis_x") && args.hasChild("axis_y") && args.hasChild("axis_z") && args.hasChild("angle"))
	{
		ob->axis = Vec3f((float)args.getChildDoubleValue(parser, "axis_x"), (float)args.getChildDoubleValue(parser, "axis_y"), (float)args.getChildDoubleValue(parser, "axis_z"));
		ob->angle = (float)args.getChildDoubleValue(parser, "angle");
	}

	ob->last_modified_time = TimeStamp::currentTime();
	ob->from_remote_transform_dirty = true;
	world->addWorldObjectAsDBDirty(ob, lock);
	world->getDirtyFromRemoteObjects(lock).insert(res->second);

	markLODChunkNeedsRebuild(world, ob, lock);
	all_worlds.markAsChanged();

	return "{\"uid\":" + toString(uid.value()) + ",\"updated\":true}";
}


static const std::string tool_deleteObject(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& args, const UserID acting_user_id)
{
	if(all_worlds.isInReadOnlyMode())
		throw glare::Exception("Server is in read-only mode; cannot delete objects.");

	const std::string world_name = args.getChildStringValueWithDefaultVal(parser, "world_name", /*default=*/"");
	const UID uid((uint64)args.getChildDoubleValue(parser, "uid"));

	WorldStateLock lock(all_worlds.mutex);
	ServerWorldState* world = getWorld(all_worlds, world_name, lock);

	auto res = world->getObjects(lock).find(uid);
	if(res == world->getObjects(lock).end())
		throw glare::Exception("No object with UID " + toString(uid.value()) + " in world '" + world_name + "'");
	WorldObject* ob = res->second.ptr();

	if(!userHasObjectWritePermissions(*ob, acting_user_id, /*user_name=*/std::string(), *world, /*allow_light_mapper_bot_full_perms=*/false, lock))
		throw glare::Exception("Permission denied: cannot delete object " + toString(uid.value()) + ".");

	ob->state = WorldObject::State_Dead;
	ob->from_remote_other_dirty = true;
	world->addWorldObjectAsDBDirty(ob, lock);
	world->getDirtyFromRemoteObjects(lock).insert(res->second);

	markLODChunkNeedsRebuild(world, ob, lock);
	all_worlds.markAsChanged();

	return "{\"uid\":" + toString(uid.value()) + ",\"deleted\":true}";
}


// The static list of tools, in the format expected by an MCP tools/list response.
static const char* TOOLS_LIST_JSON = R"TOOLS([
	{
		"name": "list_worlds",
		"description": "List all worlds on this Substrata server, with object, avatar and parcel counts. The main world has the empty-string name.",
		"inputSchema": { "type": "object", "properties": {}, "required": [] }
	},
	{
		"name": "get_world_info",
		"description": "Get information about a single world: description and object/avatar/parcel counts.",
		"inputSchema": {
			"type": "object",
			"properties": { "world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." } },
			"required": []
		}
	},
	{
		"name": "list_avatars",
		"description": "List the avatars (users) currently present in a world, with their positions.",
		"inputSchema": {
			"type": "object",
			"properties": { "world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." } },
			"required": []
		}
	},
	{
		"name": "list_objects_near",
		"description": "List world objects within a given radius of a point.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"x": { "type": "number" },
				"y": { "type": "number" },
				"z": { "type": "number" },
				"radius": { "type": "number", "description": "Search radius in metres." },
				"limit": { "type": "number", "description": "Maximum number of objects to return (default 50)." }
			},
			"required": ["x", "y", "z", "radius"]
		}
	},
	{
		"name": "get_object",
		"description": "Get the details of a single world object by its UID.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"uid": { "type": "number", "description": "The UID of the object." }
			},
			"required": ["uid"]
		}
	},
	{
		"name": "create_object",
		"description": "Create a new object in a world. Acts as the user that owns the API key, subject to that user's permissions. NOTE: any referenced model_url resource must already exist on the server.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"x": { "type": "number" },
				"y": { "type": "number" },
				"z": { "type": "number" },
				"model_url": { "type": "string", "description": "URL of an existing model resource on the server (optional)." },
				"object_type": { "type": "string", "description": "One of: generic, hypercard, voxel group, spotlight, web view, video, text, portal, seat, gear item. Default generic." },
				"content": { "type": "string", "description": "Text content, for Hypercard/Text objects (optional)." },
				"scale_x": { "type": "number", "description": "Default 1." },
				"scale_y": { "type": "number", "description": "Default 1." },
				"scale_z": { "type": "number", "description": "Default 1." }
			},
			"required": ["x", "y", "z"]
		}
	},
	{
		"name": "edit_object",
		"description": "Edit the transform of an existing object. Only the provided fields are changed. Position requires x, y and z together; scale requires scale_x/y/z together; rotation requires axis_x/y/z and angle together.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"uid": { "type": "number", "description": "The UID of the object to edit." },
				"x": { "type": "number" },
				"y": { "type": "number" },
				"z": { "type": "number" },
				"scale_x": { "type": "number" },
				"scale_y": { "type": "number" },
				"scale_z": { "type": "number" },
				"axis_x": { "type": "number" },
				"axis_y": { "type": "number" },
				"axis_z": { "type": "number" },
				"angle": { "type": "number", "description": "Rotation angle in radians about the axis." }
			},
			"required": ["uid"]
		}
	},
	{
		"name": "delete_object",
		"description": "Delete an object from a world.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"uid": { "type": "number", "description": "The UID of the object to delete." }
			},
			"required": ["uid"]
		}
	},
	{
		"name": "create_cube",
		"description": "Create an axis-aligned box from the unit-cube primitive, useful for blocking out walls, floors and buildings. z is up; metres. Rotation uses axis + angle (radians), same as objects.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"pos": { "type": "object", "description": "Box centre as {x,y,z}. Provide exactly one of pos or base_pos." },
				"base_pos": { "type": "object", "description": "Centre of the bottom face as {x,y,z}; the box rests on this z. Provide exactly one of pos or base_pos." },
				"size_x": { "type": "number", "description": "Width (x) in metres. Default 1." },
				"size_y": { "type": "number", "description": "Depth (y) in metres. Default 1." },
				"size_z": { "type": "number", "description": "Height (z) in metres. Default 1." },
				"axis_x": { "type": "number" },
				"axis_y": { "type": "number" },
				"axis_z": { "type": "number", "description": "Rotation axis, default (0,0,1)." },
				"angle": { "type": "number", "description": "Rotation angle in radians about the axis. Default 0." },
				"material": { "type": "object", "description": "Optional material, e.g. {\"colour_rgb\":{\"r\":1,\"g\":1,\"b\":1}}. Supported fields: colour_rgb {r,g,b}, roughness {val}, metallic_fraction {val}, opacity {val}, emission_rgb {r,g,b}, colour_texture_url. Omitted fields use defaults." }
			},
			"required": []
		}
	},
	{
		"name": "create_cylinder",
		"description": "Create a cylinder (extending along z by default, so vertical - good for columns and pillars). z is up; metres. Rotation uses axis + angle (radians).",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"pos": { "type": "object", "description": "Centre as {x,y,z}. Provide exactly one of pos or base_pos." },
				"base_pos": { "type": "object", "description": "Centre of the bottom face as {x,y,z}; the cylinder rests on this z. Provide exactly one of pos or base_pos." },
				"radius": { "type": "number", "description": "Radius in metres. Default 0.5." },
				"height": { "type": "number", "description": "Height in metres. Default 1." },
				"axis_x": { "type": "number" },
				"axis_y": { "type": "number" },
				"axis_z": { "type": "number", "description": "Rotation axis, default (0,0,1)." },
				"angle": { "type": "number", "description": "Rotation angle in radians about the axis. Default 0." },
				"material": { "type": "object", "description": "Optional material (see create_cube)." }
			},
			"required": []
		}
	},
	{
		"name": "create_sphere",
		"description": "Create a sphere (from the icosahedron primitive). z is up; metres.",
		"inputSchema": {
			"type": "object",
			"properties": {
				"world_name": { "type": "string", "description": "Name of the world. Use the empty string for the main world." },
				"pos": { "type": "object", "description": "Centre as {x,y,z}. Provide exactly one of pos or base_pos." },
				"base_pos": { "type": "object", "description": "Point the sphere rests on as {x,y,z} (centre is one radius above it). Provide exactly one of pos or base_pos." },
				"radius": { "type": "number", "description": "Radius in metres. Default 0.5." },
				"axis_x": { "type": "number" },
				"axis_y": { "type": "number" },
				"axis_z": { "type": "number", "description": "Rotation axis, default (0,0,1)." },
				"angle": { "type": "number", "description": "Rotation angle in radians about the axis. Default 0." },
				"material": { "type": "object", "description": "Optional material (see create_cube)." }
			},
			"required": []
		}
	}
])TOOLS";


// Dispatch a tools/call request.  Returns the CallToolResult JSON.  Tool-level errors are returned as an error result (isError=true), not thrown.
static const std::string handleToolCall(ServerAllWorldsState& all_worlds, const JSONParser& parser, const JSONNode& params, const UserID acting_user_id, const std::string& acting_user_name)
{
	if(!params.hasChild("name"))
		throw glare::Exception("tools/call is missing 'name'");
	const std::string tool_name = params.getChildStringValue(parser, "name");

	conPrint("MCP: tools/call tool: '" + tool_name + "'");

	// The tools read their arguments from the "arguments" object.  If it is omitted, use an empty object node so the tools just see their default values.
	JSONNode empty_args_node;
	empty_args_node.type = JSONNode::Type_Object;
	const JSONNode& args = params.hasChild("arguments") ? params.getChildObject(parser, "arguments") : empty_args_node;

	try
	{
		if(tool_name == "list_worlds")
			return makeToolResult(tool_listWorlds(all_worlds), /*is_error=*/false);
		else if(tool_name == "get_world_info")
			return makeToolResult(tool_getWorldInfo(all_worlds, parser, args), /*is_error=*/false);
		else if(tool_name == "list_avatars")
			return makeToolResult(tool_listAvatars(all_worlds, parser, args), /*is_error=*/false);
		else if(tool_name == "list_objects_near")
			return makeToolResult(tool_listObjectsNear(all_worlds, parser, args), /*is_error=*/false);
		else if(tool_name == "get_object")
			return makeToolResult(tool_getObject(all_worlds, parser, args), /*is_error=*/false);
		else if(tool_name == "create_object")
			return makeToolResult(tool_createObject(all_worlds, parser, args, acting_user_id, acting_user_name), /*is_error=*/false);
		else if(tool_name == "edit_object")
			return makeToolResult(tool_editObject(all_worlds, parser, args, acting_user_id, acting_user_name), /*is_error=*/false);
		else if(tool_name == "delete_object")
			return makeToolResult(tool_deleteObject(all_worlds, parser, args, acting_user_id), /*is_error=*/false);
		else if(tool_name == "create_cube")
			return makeToolResult(tool_createCube(all_worlds, parser, args, acting_user_id, acting_user_name), /*is_error=*/false);
		else if(tool_name == "create_cylinder")
			return makeToolResult(tool_createCylinder(all_worlds, parser, args, acting_user_id, acting_user_name), /*is_error=*/false);
		else if(tool_name == "create_sphere")
			return makeToolResult(tool_createSphere(all_worlds, parser, args, acting_user_id, acting_user_name), /*is_error=*/false);
		else
			return makeToolResult("Unknown tool '" + tool_name + "'", /*is_error=*/true);
	}
	catch(glare::Exception& e)
	{
		conPrint("MCP: tool '" + tool_name + "' failed: " + e.what());
		return makeToolResult(e.what(), /*is_error=*/true);
	}
}


void handleMCPRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!world_state.server_config.enable_mcp_server)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "MCP server is not enabled on this server.");
		return;
	}

	// Get API key from the Authorization header.  NOTE: HTTP header names are case-insensitive, and clients (e.g. any using
	// HTTP/2) may send them lower-cased, so match case-insensitively.
	std::string api_key;
	std::string username, password;
	for(size_t i=0; i<request.headers.size(); ++i)
	{
		if(StringUtils::equalCaseInsensitive(request.headers[i].key, "authorization"))
		{
			const std::string header_val_str = toString(request.headers[i].value);

			if(hasPrefix(::toLowerCase(header_val_str), "substrata-login"))
			{
				try
				{
					const std::string username_and_password = ::stripHeadAndTailWhitespace(header_val_str.substr(std::strlen("Substrata-Login"))); // Get rest of value
					const std::vector<std::string> components = ::split(username_and_password, '.');
					if(components.size() != 2)
						throw glare::Exception("Invalid Substrata-Login header.");

					const std::vector<unsigned char> username_bytes = StringUtils::convertHexToBinary(components[0]);
					username = std::string((const char*)username_bytes.data(), username_bytes.size());

					const std::vector<unsigned char> password_bytes = StringUtils::convertHexToBinary(components[1]);
					password = std::string((const char*)password_bytes.data(), password_bytes.size());
				}
				catch(glare::Exception&)
				{
					web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Invalid Substrata-Login credentials.");
					return;
				}
			}
			else
			{
				api_key = ::stripHeadAndTailWhitespace(header_val_str);

				// Strip the "Bearer " scheme prefix if present.  The scheme name is case-insensitive.
				if(hasPrefix(::toLowerCase(api_key), "bearer"))
					api_key = ::stripHeadAndTailWhitespace(api_key.substr(6));
			}

			break; // Once we have found an 'authorization' header, stop scanning subsequent headers.  We don't want to process more 'authorization' headers.
		}
	}

	// Authentication failures are reported as HTTP 401 Unauthorized (rather than a JSON-RPC error), which is the
	// conventional response for the HTTP transport and lets clients trigger their credential/auth flow.
	if(api_key.empty() && (username.empty() || password.empty()))
	{
		web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Missing Authorization header.  Supply an API key as 'Authorization: Bearer <key>' or use Substrata-Login");
		return;
	}

	// Check API key exists, and lookup user from API key, or use username and password passed with Substrata-Login auth scheme.
	const std::string client_ip = request.client_ip_address.toString();
	UserID user_id;
	std::string user_name;
	{
		WorldStateLock lock(world_state.mutex);

		// If this client IP has had too many recent failed authentication attempts, reject the request before doing any credential checking.
		if(world_state.server_config.do_mcp_rate_limiting)
		{
			const auto res = world_state.mcp_failed_auth_rate_limiters.find(client_ip);
			if((res != world_state.mcp_failed_auth_rate_limiters.end()) && res->second->isAtLimit(Clock::getCurTimeRealSec()))
			{
				conPrint("MCP: failed-auth rate limit exceeded for IP " + client_ip);
				writeHTTP429Response(reply_info, "Too many failed authentication attempts.  Try again later.", /*retry_after_s=*/(int)MCP_FAILED_AUTH_RATE_LIMIT_PERIOD_S);
				return;
			}
		}

		//---------------- Get User ID based on API key or username and password -------------------
		if(!api_key.empty())
		{
			const std::string key_hash = APIKey::hashAPIKey(api_key);
			const auto res = world_state.api_keys.find(key_hash);
			if(res == world_state.api_keys.end())
			{
				recordFailedAuthAttempt(world_state, client_ip, lock);
				web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Invalid API key.");
				return;
			}
			const APIKey* key = res->second.ptr();

			user_id = key->user_id;
		}
		else // Else use username and password:
		{
			const auto user_res = world_state.name_to_users.find(username);
			if(user_res == world_state.name_to_users.end())
			{
				recordFailedAuthAttempt(world_state, client_ip, lock);
				web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Invalid username or password");
				return;
			}
			const User* user = user_res->second.ptr();

			// Check password
			if(!user->isPasswordValid(password))
			{
				recordFailedAuthAttempt(world_state, client_ip, lock);
				web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Invalid username or password");
				return;
			}

			user_id = user->id;
		}
		//----------------------------------------------------------------------------------------------

		// Lookup the user that owns the key.  If the user no longer exists (e.g. was deleted), treat the key as invalid.
		auto user_res = world_state.user_id_to_users.find(user_id);
		if(user_res == world_state.user_id_to_users.end())
		{
			recordFailedAuthAttempt(world_state, client_ip, lock);
			web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Invalid API key (owning user not found).");
			return;
		}
		const User* user = user_res->second.ptr();

		user_name = user->name;

		// Rate-limit per user.  We hold the world state lock here already, which guards mcp_rate_limiters.
		if(world_state.server_config.do_mcp_rate_limiting)
		{
			auto rl_res = world_state.mcp_rate_limiters.find(user_id);
			RateLimiter* rate_limiter;
			if(rl_res == world_state.mcp_rate_limiters.end())
			{
				rate_limiter = new RateLimiter(MCP_RATE_LIMIT_PERIOD_S, MCP_RATE_LIMIT_MAX_IN_PERIOD);
				world_state.mcp_rate_limiters.insert(std::make_pair(user_id, Reference<RateLimiter>(rate_limiter)));
			}
			else
				rate_limiter = rl_res->second.ptr();

			if(!rate_limiter->checkAddEvent(Clock::getCurTimeRealSec()))
			{
				conPrint("MCP: rate limit exceeded for user " + user_id.toString());
				writeHTTP429Response(reply_info, "Rate limit exceeded.  Try again later.", /*retry_after_s=*/(int)MCP_RATE_LIMIT_PERIOD_S);
				return;
			}
		}
	}



	// Parse the JSON-RPC request from the POST body.
	JSONParser parser;
	try
	{
		parser.parseBuffer((const char*)request.post_content.data(), request.post_content.size());
	}
	catch(glare::Exception& e)
	{
		writeError(reply_info, RequestID(), JSONRPC_PARSE_ERROR, "Parse error: " + e.what());
		return;
	}

	if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
	{
		writeError(reply_info, RequestID(), JSONRPC_INVALID_REQUEST, "Expected a JSON-RPC object. (Batch requests are not supported.)");
		return;
	}

	const JSONNode& root = parser.nodes[0];

	// Extract the request id, if present.
	RequestID id;
	if(root.hasChild("id"))
	{
		const JSONNode& id_node = root.getChildNode(parser, "id");
		if(id_node.type == JSONNode::Type_Number)      { id.type = RequestID::Type_Number; id.num_val = id_node.getDoubleValue(); }
		else if(id_node.type == JSONNode::Type_String) { id.type = RequestID::Type_String; id.str_val = id_node.getStringValue(); }
	}

	if(!root.hasChild("method"))
	{
		writeError(reply_info, id, JSONRPC_INVALID_REQUEST, "Missing 'method'.");
		return;
	}
	const std::string method = root.getChildStringValue(parser, "method");

	conPrint("MCP: request method: '" + method + "'");

	// A request without an id is a notification (e.g. notifications/initialized); we just acknowledge it with an empty body.
	if(id.type == RequestID::Type_None)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, std::string());
		return;
	}

	try
	{
		if(method == "initialize")
		{
			const std::string result = "{\"protocolVersion\":\"" + std::string(MCP_PROTOCOL_VERSION) + "\""
				",\"capabilities\":{\"tools\":{}}"
				",\"serverInfo\":{\"name\":\"Substrata MCP Server\",\"version\":\"1.0.0\"}}";
			writeResult(reply_info, id, result);
		}
		else if(method == "ping")
		{
			writeResult(reply_info, id, "{}");
		}
		else if(method == "tools/list")
		{
			writeResult(reply_info, id, "{\"tools\":" + std::string(TOOLS_LIST_JSON) + "}");
		}
		else if(method == "tools/call")
		{
			if(!root.hasChild("params"))
			{
				writeError(reply_info, id, JSONRPC_INVALID_PARAMS, "Missing 'params'.");
				return;
			}
			const std::string result = handleToolCall(world_state, parser, root.getChildObject(parser, "params"), user_id, user_name);
			writeResult(reply_info, id, result);
		}
		else
		{
			writeError(reply_info, id, JSONRPC_METHOD_NOT_FOUND, "Method not found: " + method);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MCP: error handling method '" + method + "': " + e.what());
		writeError(reply_info, id, JSONRPC_INTERNAL_ERROR, e.what());
	}
}


} // end namespace MCPHandlers
