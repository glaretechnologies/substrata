/*=====================================================================
MCPRenderRequest.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <maths/vec3.h>
#include <graphics/ImageMap.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include <utils/Timer.h>
#include <string>


/*=====================================================================
MCPRenderRequest
----------------
A request to render the world from a given camera, handed from an MCP handler
thread to the GUI thread (which owns the GL context and does the actual
rendering), with the resulting image handed back.

The requesting thread fills in the inputs, enqueues the request with
MainWindow::enqueueMCPRenderRequest(), then blocks on 'condition' until 'done'.
See MainWindow::processMCPRenderRequests().
=====================================================================*/
class MCPRenderRequest : public ThreadSafeRefCounted
{
public:
	MCPRenderRequest() : width(1024), height(768), started(false), became_loaded(false), done(false), success(false) {}

	// Inputs (set by the requesting thread before enqueuing):
	Vec3d cam_pos;
	Vec3d cam_angles; // (heading, pitch, roll), radians.  See CameraController.
	int width, height;

	// GUI-thread-only driver state:
	bool started;        // Has the streaming camera been positioned yet?
	bool became_loaded;  // Has isSceneFullyLoaded() returned true since positioning?
	Timer total_timer;   // Time since the request started being processed (for the timeout).
	Timer settle_timer;  // Time since the scene became loaded (to let shadow maps etc. settle).

	// Result handoff (guarded by mutex):
	Mutex mutex;
	Condition condition;
	bool done;                     // Set true once rendering has completed (or failed).
	bool success;                  // Valid when done: true if result_image is set.
	std::string error_msg;         // Valid when done && !success.
	ImageMapUInt8Ref result_image; // Valid when done && success.
};
typedef Reference<MCPRenderRequest> MCPRenderRequestRef;
