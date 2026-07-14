/*=====================================================================
MCPClientHandler.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPClientHandler.h"


#include "MainWindow.h"
#include "MCPRenderRequest.h"
#include <webserver/RequestInfo.h>
#include <webserver/ResponseUtils.h>
#include <webserver/Escaping.h>
#include <networking/HTTPClient.h>
#include <networking/IPAddress.h>
#include <graphics/jpegdecoder.h>
#include <maths/vec3.h>
#include <utils/JSONParser.h>
#include <utils/Base64.h>
#include <utils/BufferOutStream.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Lock.h>
#include <utils/Exception.h>


// The render_view tool definition, appended to the server's tools/list response (see spliceRenderViewIntoToolsList).
static const char* RENDER_VIEW_TOOL_JSON =
	"{"
		"\"name\":\"render_view\","
		"\"description\":\"Render an image of the currently-connected world from a given camera, and return it as an image. "
			"Use this to see what the world looks like, e.g. to check what you have built. Rendering moves the streaming camera "
			"and waits for the world to finish loading around it, so this can take a few seconds.\","
		"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"cam_pos\":{\"type\":\"object\",\"description\":\"Camera position as {x,y,z} in metres (z is up).\"},"
			"\"cam_angles\":{\"type\":\"object\",\"description\":\"Camera orientation as {heading,pitch,roll} in radians. heading rotates in the x-y plane from +x towards +y (0 looks along +x, pi/2 looks along +y). pitch is a POLAR angle from the +z (up) axis: 0 looks straight up, pi/2 (~1.571) is level/horizontal, pi (~3.14) looks straight down. roll is usually 0.\"},"
			"\"width\":{\"type\":\"number\",\"description\":\"Image width in pixels (default 1024).\"},"
			"\"height\":{\"type\":\"number\",\"description\":\"Image height in pixels (default 768).\"}"
		"},\"required\":[\"cam_pos\",\"cam_angles\"]}"
	"}";


MCPClientRequestHandler::MCPClientRequestHandler(MainWindow* main_window_, const std::string& api_key_)
:	main_window(main_window_), api_key(api_key_)
{
	http_client = new HTTPClient();
	http_client->setKeepAlive(true); // Reuse the TCP (and TLS) connection to the server across forwarded calls.
	http_client->additional_headers.push_back("Authorization: Bearer " + api_key_);
}


static bool isLoopbackAddress(const IPAddress& addr)
{
	const std::string s = addr.toString();
	return s == "127.0.0.1" || s == "::1";
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


// Insert the render_view tool into a server tools/list response's "tools" array.
static const std::string spliceRenderViewIntoToolsList(const std::string& response)
{
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

	std::string insertion = std::string(RENDER_VIEW_TOOL_JSON);
	if(!empty_array)
		insertion += ",";

	return response.substr(0, insert_pos) + insertion + response.substr(insert_pos);
}


std::string MCPClientRequestHandler::forwardToServer(const std::string& request_body)
{
	// Forward to the /mcp endpoint of the server the client is currently connected to.
	// Use https for remote servers: the server webserver 301-redirects non-localhost http requests to https, and HTTPClient doesn't follow redirects for POST requests.
	const std::string hostname = main_window->gui_client.server_hostname;
	if(hostname.empty())
		throw glare::Exception("Not connected to a server.");
	const std::string server_mcp_url = ((hostname == "localhost") ? "http://" : "https://") + hostname + "/mcp";

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

		main_window->gui_client.msg_queue.enqueue(new InfoMessage("Doing MCP render..."));

		// Hand the render off to the GUI thread and wait for it.
		MCPRenderRequestRef req = new MCPRenderRequest();
		req->cam_pos = cam_pos;
		req->cam_angles = cam_angles;
		req->width = width;
		req->height = height;

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
			if(params.hasChild("name") && (params.getChildStringValue(parser, "name") == "render_view"))
			{
				handleRenderView(parser, root, reply_info);
				return;
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

		if(method == "tools/list") // Advertise our local render_view tool alongside the server's tools.
			response = spliceRenderViewIntoToolsList(response);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, response.data(), response.size(), /*content type=*/"application/json");
	}
	catch(glare::Exception& e)
	{
		const std::string s = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"" +
			web::Escaping::JSONEscape("Forwarding to Substrata server failed: " + std::string(e.what())) + "\"}}";
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, s.data(), s.size(), /*content type=*/"application/json");
	}
}
