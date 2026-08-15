/*=====================================================================
ImGUIDrawing.cpp
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ImGUIDrawing.h"


#include "GUIClient.h"
#include <utils/StringUtils.h>
#include <imgui.h>


ImGUIDrawing::ImGUIDrawing(GUIClient* gui_client_)
:	gui_client(gui_client_),
	show_frame_time_graphs(false),
	do_graphics_diagnostics(false),
	do_physics_diagnostics(false),
	do_terrain_diagnostics(false),
	first_draw(true)
{
}


ImGUIDrawing::~ImGUIDrawing()
{
}


static int cur_debug_tex_index = 0;
static bool ssao_enabled = false;


void ImGUIDrawing::drawWindows(double last_timerEvent_CPU_work_elapsed, double last_updateGL_time)
{
	if(first_draw)
	{
		ssao_enabled = gui_client->opengl_engine->isSSAOEnabled();
		first_draw = false;
	}
	//ImGui::ShowDemoWindow();

	ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(600, 900), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);
	if(ImGui::Begin("Info"))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if(ImGui::CollapsingHeader("Graphics engine"))
		{
			if(ImGui::Checkbox("use SSR and SSGI", &ssao_enabled))
				gui_client->opengl_engine->setSSAOEnabled(ssao_enabled);

			const char** names      = gui_client->opengl_engine->getDebugPassViewNames();
			const size_t names_size = gui_client->opengl_engine->getDebugPassViewNamesSize();

			if(ImGui::Combo("Debug pass view", &cur_debug_tex_index, names, (int)names_size))
			{
				gui_client->opengl_engine->setCurDebugTexIndex(cur_debug_tex_index);
			}

			ImGui::Spacing();

			ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
			if(ImGui::CollapsingHeader("Irradiance probes"))
			{
				ImGui::Checkbox("use probes", &gui_client->opengl_engine->use_probe_grid);
				ImGui::Checkbox("use probe visibility", &gui_client->opengl_engine->use_probe_visibility);
				ImGui::Checkbox("draw debug spheres", &gui_client->opengl_engine->draw_probe_debug_spheres);
				ImGui::Checkbox("update probes", &gui_client->opengl_engine->probe_updates_enabled);

#if !defined(EMSCRIPTEN) // debugDumpProbeCapture() uses glReadPixels on a float buffer, so it's desktop-only.
				// Capture has to run after draw(), since the env shader reads sun direction and radiance from the
				// per-frame MaterialCommonUniforms block.  Deferred to just after draw() next frame.
				if(ImGui::Button("capture probe at camera"))
				{
					try
					{
						const std::string dir = "d:/files";

						gui_client->opengl_engine->captureProbe(gui_client->cam_controller.getPosition().toVec4fPoint());
						gui_client->opengl_engine->debugDumpProbeCapture(dir + "/probe_capture.exr");

						// Convolve into probe 1.  Probe 0 is the global sky probe, which the sky bake owns.
						gui_client->opengl_engine->convolveProbeCaptureToTile(/*probe_index=*/1);
						gui_client->opengl_engine->debugDumpProbeAtlas(dir + "/probe_atlas.exr");

						last_probe_capture_msg = "Wrote probe_capture and probe_atlas to " + dir;
					}
					catch(glare::Exception& e)
					{
						last_probe_capture_msg = "Capture failed: " + e.what();
					}
				}

				if(ImGui::Button("clear probe irradiance atlas"))
				{
					try
					{
						gui_client->opengl_engine->clearProbeIrradianceAtlas();
					}
					catch(glare::Exception& e)
					{
						last_probe_capture_msg = "clearProbeIrradianceAtlas failed: " + e.what();
					}
				}

				if(ImGui::Button("capture complete probe grid at camera"))
				{
					try
					{
						Timer capture_timer;
						gui_client->opengl_engine->captureProbeGrid(gui_client->cam_controller.getPosition().toVec4fPoint());
						const double elapsed = capture_timer.elapsed();

						gui_client->opengl_engine->debugDumpProbeAtlas("d:/files/probe_atlas.exr");

						last_probe_capture_msg = "Captured probe grid in " + doubleToStringNDecimalPlaces(elapsed * 1000.0, 1) + " ms";
					}
					catch(glare::Exception& e)
					{
						last_probe_capture_msg = "Grid capture failed: " + e.what();
					}
				}

				//if(ImGui::Button("capture probe grid at camera"))
				// capture_probe_grid_at_camera = true;

				if(!last_probe_capture_msg.empty())
					ImGui::TextUnformatted(last_probe_capture_msg.c_str());
#endif // !defined(EMSCRIPTEN) 
			}
		} // end if (graphics engine) section

		ImGui::TextColored(ImVec4(1,1,0,1), "Stats");
		ImGui::TextUnformatted(("FPS: " + doubleToStringNDecimalPlaces(gui_client->last_fps, 1)).c_str());

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if(ImGui::CollapsingHeader("Diagnostics"))
		{
			bool diag_changed = false;
			if(ImGui::Checkbox("graphics", &do_graphics_diagnostics)) diag_changed = true;
			if(ImGui::Checkbox("physics",  &do_physics_diagnostics))  diag_changed = true;
			if(ImGui::Checkbox("terrain",  &do_terrain_diagnostics))  diag_changed = true;

			if((diagnostics_timer.elapsed() > 1.0) || diag_changed)
			{
				last_diagnostics = gui_client->getDiagnosticsString(do_graphics_diagnostics, do_physics_diagnostics, do_terrain_diagnostics, last_timerEvent_CPU_work_elapsed, last_updateGL_time);
				diagnostics_timer.reset();
			}

			ImGui::Checkbox("show frame time graphs", &show_frame_time_graphs); // The client creates/destroys its RenderStatsWidgets to match this flag.

			ImGui::TextUnformatted(last_diagnostics.c_str());
		}
	}
	ImGui::End();
}
