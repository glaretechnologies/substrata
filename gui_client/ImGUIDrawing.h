/*=====================================================================
ImGUIDrawing.h
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <utils/ThreadSafeRefCounted.h>
#include <utils/Timer.h>
#include <string>


class GUIClient;


/*=====================================================================
ImGUIDrawing
------------
Draws the ImGUI debugging/info window contents, and holds the state of the
controls in it.

This is shared between the SDL client (SDLClient.cpp, which is also the web
client) and the Qt client (see GlWidget::paintGL() and MainWindow::buildImGuiUI()),
so that both clients show the same window.

Note that the ImGUI platform and rendering backends are per-client, it's just
the window contents that are drawn here.
=====================================================================*/
class ImGUIDrawing : public ThreadSafeRefCounted
{
public:
	ImGUIDrawing(GUIClient* gui_client_);
	~ImGUIDrawing();

	// Draws the info window.  Must be called between ImGui::NewFrame() and ImGui::Render().
	void drawWindows(double last_timerEvent_CPU_work_elapsed, double last_updateGL_time);

	bool show_frame_time_graphs; // Set from the checkbox in the info window.  The client creates/destroys its RenderStatsWidgets to match this flag.

private:
	GUIClient* gui_client;

	bool do_graphics_diagnostics;
	bool do_physics_diagnostics;
	bool do_terrain_diagnostics;

	std::string last_diagnostics; // Computing the diagnostics string is relatively expensive, so don't do it every frame.
	Timer diagnostics_timer; // Time since last_diagnostics was computed.

	std::string last_probe_capture_msg;

	bool first_draw;
};
