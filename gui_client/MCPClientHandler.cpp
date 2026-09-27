/*=====================================================================
MCPClientHandler.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPClientHandler.h"


#include "MainWindow.h"
#include "MCPRenderRequest.h"
#include "LuaBuildScript.h"
#include <webserver/RequestInfo.h>
#include <webserver/ResponseUtils.h>
#include <webserver/Escaping.h>
#include <networking/HTTPClient.h>
#include <networking/IPAddress.h>
#include <graphics/jpegdecoder.h>
#include <maths/vec3.h>
#include <utils/JSONParser.h>
#include <utils/Base64.h>
#include <utils/FileUtils.h>
#include <utils/BufferOutStream.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Lock.h>
#include <utils/Exception.h>
#include <cstring>


// The tool definitions for the tools this client handles itself, appended to the server's tools/list response (see spliceLocalToolsIntoToolsList).
static const char* RENDER_VIEW_TOOL_JSON =
	"{"
		"\"name\":\"render_view\","
		"\"description\":\"Render an image of the currently-connected world from a given camera, and return it as an image. "
			"Use this to see what the world looks like, e.g. to check what you have built. Rendering moves the streaming camera "
			"and waits for the world to finish loading around it, so this can take a few seconds.\","
		"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"cam_pos\":{\"type\":\"object\",\"description\":\"Camera position as {x,y,z} in metres (z is up). The view is rendered from "
				"exactly this point, in first person, so the user's avatar is not in the picture.\"},"
			"\"cam_angles\":{\"type\":\"object\",\"description\":\"Camera orientation as {heading,pitch,roll} in radians. heading rotates in the x-y plane from +x towards +y (0 looks along +x, pi/2 looks along +y). pitch is a POLAR angle from the +z (up) axis: 0 looks straight up, pi/2 (~1.571) is level/horizontal, pi (~3.14) looks straight down. roll is usually 0. "
				"The top of the image is towards the heading, so looking straight down with heading 0, image-up is +x and image-right is -y.\"},"
			"\"fov\":{\"type\":\"number\",\"description\":\"Horizontal field of view in radians, from 0.05 to 3.0. Defaults to the client camera's "
				"current lens, normally about 1.22 (70 degrees). Use a wide view for establishing shots and a narrow one for detail.\"},"
			"\"width\":{\"type\":\"number\",\"description\":\"Image width in pixels (default 1024).\"},"
			"\"height\":{\"type\":\"number\",\"description\":\"Image height in pixels (default 768).\"}"
		"},\"required\":[\"cam_pos\",\"cam_angles\"]}"
	"}";


static const char* RUN_LUA_BUILD_SCRIPT_TOOL_JSON =
	"{"
		"\"name\":\"run_lua_build_script\","
		"\"description\":\"Run a Luau build script in the connected client and return what it printed. "
			"The script runs once, from top to bottom, and is not attached to a world object, so it cannot define event handlers "
			"such as onUserTouchedObject. "
			"The language is Luau (as used by Roblox), but none of the Roblox API exists here: there is no Instance, Vector3, "
			"CFrame, task or game. Available globals are createObject(), createMesh(), getOrCreateGroup(), deleteObjectsInGroup(), "
			"getTerrainHeight(), getTerrainHeights(), print(), getCurrentTime(), parseJSON(), Vec3d(x,y,z) and Vec3f(x,y,z). Indexes are "
			"1-based, as in all Lua. "
			"Vec3d and Vec3f are different types and are NOT interchangeable: positions are Vec3d, everything else below is Vec3f. "
			"Distances are in metres and z is up, so a person is about 1.8 high and a doorway about 2.1. "
			"getTerrainHeight(x, y) returns the z of the ground at (x, y), so a build can fit the terrain it stands on. "
			"getTerrainHeights{x1, y1, x2, y2, ...} returns {z1, z2, ...} for many points at once; use it for more than a handful of "
			"points, since each call waits for the client's main thread. These give the height of the terrain only, not of objects "
			"on it, and asking about terrain that hasn't loaded yet is an error. "
			"createObject takes one table: model_url (required; must be a model present in the connected world, since the "
			"client reads it to work out the object's bounds), pos (Vec3d, required), axis (Vec3f, default Vec3f(0,0,1)), angle "
			"(radians, default 0), scale (Vec3f, default Vec3f(1,1,1)), collidable, dynamic, content, script, target_url, and "
			"materials (an array of material tables taking colour (Vec3f, non-linear sRGB in 0-1), colour_texture_url, emission_rgb "
			"(Vec3f), emission_texture_url, normal_map_url, roughness_val, roughness_texture_url, metallic_fraction_val, opacity_val, "
			"tex_matrix, emission_lum_flux_or_lum, hologram, double_sided). "
			"These built-in meshes are the usual building blocks, and their object-space sizes differ, so check the size before "
			"working out a scale: "
			"cube = \\\"Cube_obj_12971581758459554602.bmesh\\\", a 1x1x1 cube centred on the origin; "
			"sphere = \\\"Icosahedron_obj_17649497764207890525.bmesh\\\", diameter 1, centred; "
			"cylinder = \\\"Cylinder_obj_8542616007088785005.bmesh\\\", radius 0.25 and height 1, centred, axis along z - note the "
			"radius, so a cylinder of radius r needs scale x and y of 4*r; "
			"cone = \\\"cone_igmesh_9525996822499707335.bmesh\\\", base radius 0.5 and height 1, and unlike the others it is NOT "
			"centred on z: its base sits at z = 0 and it points towards +z, so pos is the centre of the base; "
			"wedge = \\\"wedge_igmesh_4446548145440212638.bmesh\\\", a 1x1x1 box with the thin edge at -x, sloping up towards +x. "
			"If one of these reports that the client does not have it on disk, create a single object with the matching "
			"create_cube/create_sphere/create_cylinder/create_cone/create_wedge tool first, which fetches the mesh, then the build "
			"script can use it. "
			"For anything made of many pieces, build ONE mesh instead of one object per piece: a dock of 24 planks is far better as "
			"one mesh with 24 boxes than as 24 objects. m = createMesh() gives you a mesh, and local url = m:upload(\\\"name\\\") "
			"returns a model_url to pass to createObject. Mesh coordinates are Vec3f and are relative to the object's pos, so "
			"build around the origin and place the object where you want it. "
			"Add geometry with any of: "
			"m:addBox(centre, size [, axis, angle]); "
			"m:addCylinder(bottom, top, radius [, top_radius, segments]), a cylinder between two points at any angle, capped at "
				"both ends - give a different top_radius for a tapered column, or 0 for a cone; "
			"m:addSphere(centre, radius [, segments]); "
			"m:addQuad(v0, v1, v2, v3), corners counter-clockwise seen from the visible side; "
			"or m:addTriangles{positions = {...}, normals = {...}, uvs = {...}, indices = {...}} to hand over a whole mesh at once, "
			"which is much faster than a call per face if you are generating geometry from a formula. In addTriangles, positions is "
			"required and holds x,y,z per vertex; normals (x,y,z per vertex) and uvs (u,v per vertex) are optional; indices holds 3 "
			"per triangle and is 1-BASED, like every other array in Lua, so the first vertex is 1 and not 0. With indices omitted the "
			"vertices are taken in threes. With normals omitted each triangle gets a flat normal, which splits any shared vertices. "
			"m:setMaterial(i) puts following geometry on the i'th material of the object, 1-based to match the materials array. "
			"Boxes, quads, cylinders and spheres get UVs measured in metres, so textures keep their scale across faces of different "
				"sizes. segments defaults to 32; fewer keeps the triangle count down for small or distant things. "
			"Put the objects you create in a group, and start the script by emptying it, or running a script twice leaves two copies of "
			"everything. local g = getOrCreateGroup(\\\"dock\\\") gives you the group named \\\"dock\\\", making one if this is the first "
			"run, and deleteObjectsInGroup(g) then removes what the previous run put in it and returns how many objects it removed. Pass "
			"group_id = g to createObject to put an object in the group. A group is a handle the user can grab to move everything in it "
			"at once, and it places itself on the first object you put in it, so no position is needed. Groups belong to the user whose "
			"client is running the script, so a group only ever holds, and deleteObjectsInGroup only ever removes, objects you created. "
			"A group can hold another group, and emptying the outer one empties the inner ones too. "
			"Objects are sent to the server as the script runs, and the server checks separately that you may build at that position. "
			"The result gives the total triangle and vertex count of what was sent, and says how many objects the server accepted and how "
			"many it refused, with the reason for the first refusal - so if objects are "
			"reported as REFUSED, the build did not land and the position or your permission to build there is the thing to fix. "
			"Errors are returned as text: compile errors carry a line and column, runtime errors carry the line they were raised on. "
			"\\n\\nHere is the shape a build should take - one object, one mesh, two materials, and a group so that re-running replaces "
			"what it made last time:\\n"
			"\\n"
			"local g = getOrCreateGroup(\\\"dock\\\")\\n"
			"deleteObjectsInGroup(g)\\n"
			"\\n"
			"local m = createMesh()\\n"
			"m:setMaterial(1) -- planks\\n"
			"for i = 0, 23 do\\n"
			"  m:addBox(Vec3f(0, i * 0.32, 0), Vec3f(2.4, 0.30, 0.06))\\n"
			"end\\n"
			"m:setMaterial(2) -- posts\\n"
			"for i = 0, 3 do\\n"
			"  m:addBox(Vec3f(-1.1, i * 2.2, -0.6), Vec3f(0.18, 0.18, 1.2))\\n"
			"end\\n"
			"\\n"
			"createObject({\\n"
			"  model_url = m:upload(\\\"dock\\\"),\\n"
			"  pos = Vec3d(120, 600, 0),\\n"
			"  group_id = g,\\n"
			"  materials = {\\n"
			"    { colour = Vec3f(0.55, 0.40, 0.25), roughness_val = 0.8 },\\n"
			"    { colour = Vec3f(0.35, 0.33, 0.30), roughness_val = 0.9 },\\n"
			"  },\\n"
			"})\\n"
			"\\n"
			"That is 28 planks and posts as one object. As 28 separate objects it would be slower to load, slower to re-run, and "
			"harder to move.\","
		"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"script_path\":{\"type\":\"string\",\"description\":\"Absolute path to a file holding the Luau source, on the machine running the Substrata client. "
				"Prefer this over 'script': keeping the build in a file lets you edit a line and re-run instead of resending the whole thing, and errors refer to a file you can open.\"},"
			"\"script\":{\"type\":\"string\",\"description\":\"The Luau source itself, for a one-off you don't want to keep. Provide either this or script_path, not both. "
				"Does not need the --lua prefix that object scripts use.\"}"
		"}}"
	"}";


MCPClientRequestHandler::MCPClientRequestHandler(MainWindow* main_window_, const std::string& username, const std::string& password)
:	main_window(main_window_)
{
	http_client = new HTTPClient();
	http_client->setKeepAlive(true); // Reuse the TCP (and TLS) connection to the server across forwarded calls.

	http_client->additional_headers.push_back("Authorization: Substrata-Login " + 
		StringUtils::convertByteArrayToHexString((const unsigned char*)username.data(), username.size()) + "." + 
		StringUtils::convertByteArrayToHexString((const unsigned char*)password.data(), password.size())
	);
}


static bool isLoopbackAddress(const IPAddress& addr)
{
	// The listening socket is dual-stack, so a client connecting over IPv4 shows up as the IPv4-mapped address ::ffff:127.0.0.1.
	const std::string s = addr.toString();
	return s == "127.0.0.1" || s == "::ffff:127.0.0.1" || s == "::1";
}


// Serialise the JSON-RPC request 'id' (number, string, or absent) so it can be echoed back in the response.
static const std::string extractIdJSON(const JSONParser& parser, const JSONNode& root)
{
	if(!root.hasChild("id"))
		return "null";
	const JSONNode& id_node = root.getChildNode(parser, "id");
	if(id_node.type == JSONNode::Type_String)
		return "\"" + web::Escaping::JSONEscape(id_node.getStringValue()) + "\"";
	else if(id_node.type == JSONNode::Type_Number)
	{
		const double v = id_node.getDoubleValue();
		if(v == (double)(int64)v)
			return toString((int64)v);
		else
			return doubleToString(v);
	}
	else
		return "null";
}


static void writeJSONRPCResult(web::ReplyInfo& reply_info, const std::string& id_json, const std::string& result_json)
{
	const std::string s = "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"result\":" + result_json + "}";
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, s.data(), s.size(), /*content type=*/"application/json");
}


// The tool JSON above is hand-written inside C++ string literals, where an escaping mistake is easy to make, invisible to read,
// and would break tools/list entirely rather than just spoiling one description.  So check it parses before we send it.
static void checkToolJSONValid(const char* tool_json)
{
	try
	{
		JSONParser parser;
		parser.parseBuffer(tool_json, std::strlen(tool_json));

		assert(!parser.nodes.empty() && (parser.nodes[0].type == JSONNode::Type_Object));
	}
	catch(glare::Exception& e)
	{
		conPrint("Tool JSON is not valid JSON: " + std::string(e.what()));
		assert(0);
	}
}


// Insert the locally-handled tools into a server tools/list response's "tools" array.
static const std::string spliceLocalToolsIntoToolsList(const std::string& response)
{
	checkToolJSONValid(RENDER_VIEW_TOOL_JSON);
	checkToolJSONValid(RUN_LUA_BUILD_SCRIPT_TOOL_JSON);

	const std::string marker = "\"tools\":[";
	const size_t pos = response.find(marker);
	if(pos == std::string::npos)
		return response; // Unexpected shape; return unchanged.

	const size_t insert_pos = pos + marker.size();

	// Determine whether the array is empty (next non-whitespace char is ']'), so we know whether to add a separating comma.
	size_t k = insert_pos;
	while(k < response.size() && isWhitespace(response[k]))
		k++;
	const bool empty_array = (k < response.size()) && (response[k] == ']');

	std::string insertion = std::string(RENDER_VIEW_TOOL_JSON) + "," + std::string(RUN_LUA_BUILD_SCRIPT_TOOL_JSON);
	if(!empty_array)
		insertion += ",";

	return response.substr(0, insert_pos) + insertion + response.substr(insert_pos);
}


std::string MCPClientRequestHandler::forwardToServer(const std::string& request_body)
{
	// Forward to the /mcp endpoint of the server the client is currently connected to.
	const std::string hostname = main_window->gui_client.server_hostname;
	if(hostname.empty())
		throw glare::Exception("Not connected to a server.");
	const std::string server_mcp_url = "https://" + hostname + "/mcp";

	// sendPost() connects on first use, reuses the connection on subsequent calls (keep-alive), and reconnects if the
	// hostname has changed since the last call (e.g. the user teleported to a different server).
	for(int attempt = 0; attempt < 2; ++attempt)
	{
		try
		{
			std::string response;
			http_client->sendPost(server_mcp_url, request_body, /*content type=*/"application/json", response); // Throws glare::Exception on failure.
			return response;
		}
		catch(HTTPClientExcep& e)
		{
			http_client->resetConnection(); // Don't reuse a socket that may be in an inconsistent state.

			// If the server closed the idle keep-alive connection since the last request, the request wasn't processed,
			// so it's safe to retry it once on a fresh connection.
			if(!((e.excepType() == HTTPClientExcep::ExcepType_ConnectionClosedGracefully) && (attempt == 0)))
				throw;
		}
		catch(glare::Exception&)
		{
			http_client->resetConnection(); // Don't reuse a socket that may be in an inconsistent state.
			throw;
		}
	}

	throw glare::Exception("Unreachable"); // Keep the compiler happy about the missing return.
}


void MCPClientRequestHandler::handleRenderView(const JSONParser& parser, const JSONNode& root, web::ReplyInfo& reply_info)
{
	const std::string id_json = extractIdJSON(parser, root);

	try
	{
		const JSONNode& params = root.getChildObject(parser, "params");
		if(!params.hasChild("arguments"))
			throw glare::Exception("render_view requires 'arguments'.");
		const JSONNode& args = params.getChildObject(parser, "arguments");

		const JSONNode& cam_pos_node = args.getChildObject(parser, "cam_pos");
		const Vec3d cam_pos(cam_pos_node.getChildDoubleValue(parser, "x"), cam_pos_node.getChildDoubleValue(parser, "y"), cam_pos_node.getChildDoubleValue(parser, "z"));

		const JSONNode& ang_node = args.getChildObject(parser, "cam_angles");
		const Vec3d cam_angles(
			ang_node.getChildDoubleValue(parser, "heading"),
			ang_node.getChildDoubleValue(parser, "pitch"),
			ang_node.getChildDoubleValueWithDefaultVal(parser, "roll", /*default=*/0.0));

		const int width  = (int)args.getChildDoubleValueWithDefaultVal(parser, "width",  /*default=*/1024);
		const int height = (int)args.getChildDoubleValueWithDefaultVal(parser, "height", /*default=*/768);
		if(width < 16 || width > 4096 || height < 16 || height > 4096)
			throw glare::Exception("width/height out of range [16, 4096].");

		const double fov = args.getChildDoubleValueWithDefaultVal(parser, "fov", /*default=*/0.0); // 0 means use the camera's current lens.
		if(args.hasChild("fov") && !(fov >= 0.05 && fov <= 3.0))
			throw glare::Exception("fov out of range [0.05, 3.0] radians.");

		main_window->gui_client.msg_queue.enqueue(new InfoMessage("Doing MCP render..."));

		// Hand the render off to the GUI thread and wait for it.
		MCPRenderRequestRef req = new MCPRenderRequest();
		req->cam_pos = cam_pos;
		req->cam_angles = cam_angles;
		req->width = width;
		req->height = height;
		req->horizontal_fov = (float)fov;

		main_window->enqueueMCPRenderRequest(req);

		Timer wait_timer; // Handler-side timeout (the request's own timers are owned by the GUI thread).
		{
			Lock lock(req->mutex);
			while(!req->done)
			{
				req->condition.waitWithTimeout(req->mutex, /*wait_time_seconds=*/5.0);
				if(!req->done && (wait_timer.elapsed() > 90.0)) // Safety net in case the GUI thread never fulfils the request.
					throw glare::Exception("Timed out waiting for render.");
			}
		}

		if(!req->success)
			throw glare::Exception(req->error_msg);

		// Encode the rendered image as JPEG in memory, then base64 for the MCP image content block.
		BufferOutStream buf;
		JPEGDecoder::saveToStream(req->result_image, JPEGDecoder::SaveOptions(/*quality=*/90), buf);

		std::string b64;
		Base64::encode(buf.buf.data(), buf.buf.size(), b64);

		const std::string result = "{\"content\":[{\"type\":\"image\",\"data\":\"" + b64 + "\",\"mimeType\":\"image/jpeg\"}],\"isError\":false}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
	catch(glare::Exception& e)
	{
		conPrint("MCP client: render_view failed: " + e.what());
		const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(e.what()) + "\"}],\"isError\":true}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
}


void MCPClientRequestHandler::handleRunLuaBuildScript(const JSONParser& parser, const JSONNode& root, web::ReplyInfo& reply_info)
{
	const std::string id_json = extractIdJSON(parser, root);

	try
	{
		const JSONNode& params = root.getChildObject(parser, "params");
		if(!params.hasChild("arguments"))
			throw glare::Exception("run_lua_build_script requires 'arguments'.");
		const JSONNode& args = params.getChildObject(parser, "arguments");

		const bool has_script      = args.hasChild("script");
		const bool has_script_path = args.hasChild("script_path");
		if(has_script == has_script_path)
			throw glare::Exception("Provide exactly one of 'script' or 'script_path'.");

		std::string script_src;
		std::string script_desc; // Echoed in the result, so the caller can tell which source was run.
		LuaBuildScriptOptions build_options;
		if(has_script_path)
		{
			const std::string script_path = args.getChildStringValue(parser, "script_path");

			// The agent's working directory and this client's are unrelated, so a relative path would resolve somewhere the
			// caller didn't mean.
			if(!FileUtils::isPathAbsolute(script_path))
				throw glare::Exception("'script_path' must be an absolute path.");
			if(!FileUtils::fileExists(script_path))
				throw glare::Exception("No such file: '" + script_path + "'.");

			script_src = FileUtils::readEntireFileTextMode(script_path); // Throws FileUtilsExcep on failure.
			script_desc = "Ran " + script_path + " (" + toString(script_src.size()) + " bytes).\n";
			build_options.script_name = script_path; // So errors name the file, not just a line number.
		}
		else
			script_src = args.getChildStringValue(parser, "script");

		// Build scripts read world state (e.g. getCurrentTime), so require a connected world.
		if(main_window->gui_client.world_state.isNull())
			throw glare::Exception("Not connected to a world.");

		main_window->gui_client.msg_queue.enqueue(new InfoMessage("Running MCP Lua build script..."));

		LuaBuildScriptResults results;
		LuaBuildScript::run(&main_window->gui_client, main_window->gui_client.lua_builder_state.ptr(), script_src, build_options, results);

		// Report the run as text: what the script printed, then how it finished.  Agents fix what they can read, so the error text
		// (which carries source locations) goes in the result rather than just the log.
		std::string text = script_desc + results.output;
		if(results.output_truncated)
			text += "\n[output truncated]";

		// A new group's group object is counted separately, so a script that makes one object doesn't appear to have made two.
		std::string obs_created = toString(results.num_objects_created - results.num_group_objects_created) + " object(s)";
		if(results.num_group_objects_created > 0)
			obs_created += " and " + toString(results.num_group_objects_created) + " new group object(s)";
		obs_created += " sent to the server";
		if(results.num_objects_created > 0)
			obs_created += " (" + toString(results.num_tris_created) + " triangles, " + toString(results.num_verts_created) + " vertices)";
		obs_created += ", " + toString(results.num_objects_deleted) + " removed";

		if(results.num_objects_created > 0)
		{
			if(results.num_creates_refused > 0)
				obs_created += ", " + toString(results.num_creates_refused) + " REFUSED by the server: " + results.first_refusal_msg;
			else if(results.num_creates_answered < results.num_objects_created)
				obs_created += ", " + toString(results.num_creates_answered) + " confirmed before we stopped waiting for the server";
			else
				obs_created += ", all confirmed created";
		}

		if(results.success)
			text += "\nScript completed in " + doubleToStringNSigFigs(results.elapsed_s, 3) + " s, " + obs_created + ".";
		else
			text += "\nScript failed after " + doubleToStringNSigFigs(results.elapsed_s, 3) + " s, " + obs_created + " before it failed:\n" + results.error_msg;

		const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(text) + "\"}],\"isError\":" +
			(results.success ? "false" : "true") + "}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
	catch(glare::Exception& e)
	{
		conPrint("MCP client: run_lua_build_script failed: " + e.what());
		const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(e.what()) + "\"}],\"isError\":true}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
}


void MCPClientRequestHandler::handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	// Only serve local requests.
	if(!isLoopbackAddress(request_info.client_ip_address))
	{
		web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "The MCP endpoint may only be accessed from localhost.");
		return;
	}

	const std::string body((const char*)request_info.post_content.data(), request_info.post_content.size());

	// Parse the request to find the method, and (for tools/call) the tool name, so we can route render_view locally.
	std::string method;
	try
	{
		JSONParser parser;
		parser.parseBuffer(body.data(), body.size());
		if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
			throw glare::Exception("Expected a JSON-RPC object.");
		const JSONNode& root = parser.nodes[0];

		if(root.hasChild("method"))
			method = root.getChildStringValue(parser, "method");

		if(method == "tools/call" && root.hasChild("params"))
		{
			const JSONNode& params = root.getChildObject(parser, "params");
			if(params.hasChild("name"))
			{
				const std::string tool_name = params.getChildStringValue(parser, "name");
				if(tool_name == "render_view")
				{
					handleRenderView(parser, root, reply_info);
					return;
				}
				else if(tool_name == "run_lua_build_script")
				{
					handleRunLuaBuildScript(parser, root, reply_info);
					return;
				}
			}
		}
	}
	catch(glare::Exception&)
	{
		// Not parseable as something we handle locally; fall through and let the server deal with it.
	}

	// Forward everything else to the Substrata server's /mcp endpoint.
	try
	{
		main_window->gui_client.msg_queue.enqueue(new InfoMessage("Handling MCP '" + method + "' method."));

		std::string response = forwardToServer(body);

		if(method == "tools/list") // Advertise our local tools alongside the server's tools.
			response = spliceLocalToolsIntoToolsList(response);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, response.data(), response.size(), /*content type=*/"application/json");
	}
	catch(glare::Exception& e)
	{
		const std::string s = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"" +
			web::Escaping::JSONEscape("Forwarding to Substrata server failed: " + std::string(e.what())) + "\"}}";
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, s.data(), s.size(), /*content type=*/"application/json");
	}
}
