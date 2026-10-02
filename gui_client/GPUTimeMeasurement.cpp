/*=====================================================================
GPUTimeMeasurement.cpp
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GPUTimeMeasurement.h"


#include "GUIClient.h"
#include <utils/FileUtils.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <algorithm>
#include <limits>


GPUTimeMeasurement::GPUTimeMeasurement(const std::string& output_path_, const Vec3d& cam_pos_, const Vec3d& cam_angles_, int num_frames_)
:	output_path(output_path_),
	cam_pos(cam_pos_),
	cam_angles(cam_angles_),
	num_frames(num_frames_),
	became_loaded(false),
	load_timed_out(false),
	measuring(false),
	was_profiling_enabled(false),
	num_warmup_frames_done(0),
	connected(false)
{}


bool GPUTimeMeasurement::think(GUIClient& gui_client, OpenGLEngine& engine)
{
	// How long to wait after the scene finishes loading before measuring, to let shadow maps etc. settle.
	const double SETTLE_S = 2.0;
	// Minimum time to wait after connecting, to allow the initial object-query response to arrive (as for the screenshot code).
	const double MIN_WAIT_S = 4.0;
	// Measure anyway after this long, even if loading hasn't settled (e.g. a resource is unavailable).  The report says so.
	const double TIMEOUT_S = 120.0;
	// Results of the timer queries come back a frame or two late, so let a few frames go by after enabling profiling before sampling.
	const int NUM_WARMUP_FRAMES = 30;

	if(gui_client.connection_state != GUIClient::ServerConnectionState_Connected || gui_client.world_state.isNull())
		return false;

	if(!connected)
	{
		connected = true;
		total_timer.reset();

		gui_client.player_physics.setEyePosition(cam_pos);
		gui_client.player_physics.setFlyModeEnabled(true); // Don't fall to the ground.
	}

	// Hold the camera at the measurement position every frame, so that nothing else (the mouse, say) moves it.
	gui_client.cam_controller.setAngles(cam_angles);
	gui_client.cam_controller.setFirstAndThirdPersonPositions(cam_pos);

	if(!measuring)
	{
		const bool loaded = gui_client.isSceneFullyLoaded();
		if(loaded && !became_loaded)
		{
			became_loaded = true;
			settle_timer.reset();
		}
		else if(!loaded)
			became_loaded = false; // More streaming was triggered; wait again.

		const bool ready = became_loaded && (settle_timer.elapsed() >= SETTLE_S) && (total_timer.elapsed() >= MIN_WAIT_S);
		if(!ready)
		{
			if(total_timer.elapsed() < TIMEOUT_S)
				return false;
			load_timed_out = true;
		}

		was_profiling_enabled = engine.isProfilingEnabled();
		engine.setProfilingEnabled(true);
		if(!engine.isProfilingEnabled())
		{
			writeReport("Error: GPU timer queries are not supported on this platform.\n");
			return true;
		}

		conPrint("GPUTimeMeasurement: measuring " + toString(num_frames) + " frames...");
		measuring = true;
		return false;
	}

	if(num_warmup_frames_done < NUM_WARMUP_FRAMES)
	{
		num_warmup_frames_done++;
		if(num_warmup_frames_done == NUM_WARMUP_FRAMES)
			measure_timer.reset();
		return false;
	}

	gpu_time_samples.push_back(engine.getLastGPUPassTimes());
	draw_count_samples.push_back(engine.getLastDrawCounts());
	for(int i=0; i<OpenGLEngine::NUM_GPU_SECTIONS; ++i)
		section_time_samples[i].push_back(engine.getLastGPUSectionTime((OpenGLEngine::GPUSection)i));

	if((int)gpu_time_samples.size() < num_frames)
		return false;

	const double wall_time_s = measure_timer.elapsed();

	if(!was_profiling_enabled)
		engine.setProfilingEnabled(false);

	writeReport(makeReport(engine, wall_time_s));
	return true;
}


// Mean, median, 90th percentile and maximum of the samples, in ms, as a table row.
static std::string statsRow(const std::string& name, std::vector<double>& samples_s, int name_width = 20)
{
	std::sort(samples_s.begin(), samples_s.end());
	const size_t n = samples_s.size();
	double sum = 0;
	for(size_t i=0; i<n; ++i)
		sum += samples_s[i];
	const double mean   = sum / n;
	const double median = samples_s[n / 2];
	const double p90    = samples_s[std::min(n - 1, (n * 9) / 10)];
	const double max    = samples_s[n - 1];

	return ::rightSpacePad(name, name_width) +
		::leftPad(doubleToStringNDecimalPlaces(mean   * 1.0e3, 3), ' ', 9) +
		::leftPad(doubleToStringNDecimalPlaces(median * 1.0e3, 3), ' ', 9) +
		::leftPad(doubleToStringNDecimalPlaces(p90    * 1.0e3, 3), ' ', 9) +
		::leftPad(doubleToStringNDecimalPlaces(max    * 1.0e3, 3), ' ', 9) + "\n";
}


std::string GPUTimeMeasurement::makeReport(const OpenGLEngine& engine, double wall_time_s) const
{
	const std::vector<OpenGLEngine::GPUPassTimes>& samples = gpu_time_samples;
	const size_t n = samples.size();

	std::string s = "Measured " + toString(n) + " frames at " + toString(engine.getViewPortWidth()) + " x " + toString(engine.getViewPortHeight()) +
		" on " + engine.opengl_renderer + "\n";
	s += "Camera: pos (" + doubleToStringNDecimalPlaces(cam_pos.x, 3) + ", " + doubleToStringNDecimalPlaces(cam_pos.y, 3) + ", " + doubleToStringNDecimalPlaces(cam_pos.z, 3) +
		"), heading " + doubleToStringNDecimalPlaces(cam_angles.x, 4) + ", pitch " + doubleToStringNDecimalPlaces(cam_angles.y, 4) + "\n";
	if(load_timed_out)
		s += "WARNING: the scene had not finished loading when measurement started, so these times may not be comparable with other runs.\n";
	s += "Wall-clock time per frame: " + doubleToStringNDecimalPlaces(wall_time_s / n * 1.0e3, 3) + " ms (limited by vsync if it is on; the GPU times are not)\n\n";

	s += ::rightSpacePad("GPU pass (ms)", 20) + ::leftPad("mean", ' ', 9) + ::leftPad("median", ' ', 9) + ::leftPad("p90", ' ', 9) + ::leftPad("max", ' ', 9) + "\n";

	std::vector<double> v(n);
#define GPU_TIME_ROW(field, name) \
	for(size_t i=0; i<n; ++i) v[i] = samples[i].field; \
	s += statsRow(name, v);

	GPU_TIME_ROW(dynamic_depth_draw, "dynamic depth draw");
	GPU_TIME_ROW(static_depth_draw,  "static depth draw");
	GPU_TIME_ROW(pre_pass,           "pre-pass");
	GPU_TIME_ROW(compute_ssao,       "compute SSAO");
	GPU_TIME_ROW(blur_ssao,          "blur SSAO");
	GPU_TIME_ROW(draw_opaque_obs,    "draw opaque obs");
	GPU_TIME_ROW(draw_water,         "draw water");
	GPU_TIME_ROW(decal_copy_buffers, "decal copy buffers");
	GPU_TIME_ROW(bloom,              "bloom");
	GPU_TIME_ROW(fog_post_process,   "fog post-process");
	GPU_TIME_ROW(final_imaging,      "final imaging");
	GPU_TIME_ROW(overlay_obs,        "overlay obs");
	GPU_TIME_ROW(total,              "TOTAL");
#undef GPU_TIME_ROW

	// Sum the passes per frame, so that the time in work outside the pass timers shows up as the difference from the total.
	for(size_t i=0; i<n; ++i)
	{
		const OpenGLEngine::GPUPassTimes& t = samples[i];
		v[i] = t.dynamic_depth_draw + t.static_depth_draw + t.pre_pass + t.compute_ssao + t.blur_ssao + t.draw_opaque_obs + t.draw_water +
			t.decal_copy_buffers + t.bloom + t.fog_post_process + t.final_imaging + t.overlay_obs;
	}
	s += statsRow("sum of passes", v);

	// Unlike the pass timers above, the sections are timed every frame and cover all of draw(), so they include the work outside the timed passes.
	s += "\n" + ::rightSpacePad("draw() section (ms)", 28) + ::leftPad("mean", ' ', 9) + ::leftPad("median", ' ', 9) + ::leftPad("p90", ' ', 9) + ::leftPad("max", ' ', 9) + "\n";
	for(int i=0; i<OpenGLEngine::NUM_GPU_SECTIONS; ++i)
	{
		std::vector<double> section_times = section_time_samples[i];
		s += statsRow(OpenGLEngine::getGPUSectionName((OpenGLEngine::GPUSection)i), section_times, /*name_width=*/28);
	}

	// Main-pass draw counts barely change between frames from a fixed camera, so the last frame's are representative.
	const OpenGLEngine::DrawCounts& c = draw_count_samples.back();
	s += "\nObjects in view frustum: " + toString(c.num_obs_in_frustum) + "\n";
	s += "Main pass: " + toString(c.num_batches_drawn) + " batches, " + uInt32ToStringCommaSeparated(c.num_tris_drawn) + " tris, " + toString(c.num_prog_changes) + " program changes\n";

	// The depth pass draws a different static cascade and object set each frame, on a 12-frame cycle, so its counts vary from frame to frame.
	uint64 batches_sum = 0, tris_sum = 0;
	uint32 min_tris = std::numeric_limits<uint32>::max(), max_tris = 0;
	for(size_t i=0; i<draw_count_samples.size(); ++i)
	{
		const OpenGLEngine::DrawCounts& d = draw_count_samples[i];
		batches_sum += d.depth_num_batches_drawn;
		tris_sum += d.depth_num_tris_drawn;
		min_tris = std::min(min_tris, d.depth_num_tris_drawn);
		max_tris = std::max(max_tris, d.depth_num_tris_drawn);
	}
	const size_t num_draw_samples = draw_count_samples.size();
	s += "Depth pass (mean per frame): " + toString(batches_sum / num_draw_samples) + " batches, " + uInt64ToStringCommaSeparated(tris_sum / num_draw_samples) + " tris (min " +
		uInt32ToStringCommaSeparated(min_tris) + ", max " + uInt32ToStringCommaSeparated(max_tris) + ")\n";

	return s;
}


void GPUTimeMeasurement::writeReport(const std::string& report)
{
	conPrint("GPUTimeMeasurement:\n" + report);
	try
	{
		FileUtils::writeEntireFileTextMode(output_path, report);
		conPrint("GPUTimeMeasurement: wrote report to '" + output_path + "'.");
	}
	catch(glare::Exception& e)
	{
		conPrint("GPUTimeMeasurement: failed to write report to '" + output_path + "': " + e.what());
	}
}
