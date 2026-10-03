/*=====================================================================
GPUTimeMeasurement.h
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLEngine.h>
#include <maths/vec3.h>
#include <utils/Timer.h>
#include <string>
#include <vector>
class GUIClient;


/*=====================================================================
GPUTimeMeasurement
------------------
Measures the GPU time of each render pass of the main view, from a fixed
camera, over a number of frames, and writes a report to a file.

Started with the --measure_gpu_times command line option.  Waits for the
client to connect and for the world around the camera to finish loading,
then measures, so that runs of different builds from the same camera can be
compared.  Also saves a render of the view next to the report, as a PNG.
=====================================================================*/
class GPUTimeMeasurement
{
public:
	GPUTimeMeasurement(const std::string& output_path, const Vec3d& cam_pos, const Vec3d& cam_angles, int num_frames);

	// Call once per frame.  Returns true once the report has been written (or writing it failed), after which the client can exit.
	bool think(GUIClient& gui_client, OpenGLEngine& engine);

private:
	std::string makeReport(const OpenGLEngine& engine, double wall_time_s) const;
	std::string makeNearbyObjectsReport(OpenGLEngine& engine) const;
	void writeReport(const std::string& report);
	void saveRender(GUIClient& gui_client, OpenGLEngine& engine);

	std::string output_path;
	Vec3d cam_pos;
	Vec3d cam_angles; // (heading, pitch, roll), radians.  See CameraController.
	int num_frames;

	bool became_loaded;      // Has the scene finished loading since the camera was positioned?
	bool load_timed_out;     // Did we give up waiting for loading, and measure anyway?
	std::string load_timeout_status; // What loading was still waiting on, if it timed out.  See GUIClient::getSceneLoadingStatus().
	Timer status_log_timer;  // Time since the loading status was last logged.
	bool measuring;
	bool was_profiling_enabled; // Profiling state before measuring, restored afterwards.
	int num_warmup_frames_done;
	Timer total_timer;       // Time since connecting, for the loading timeout.
	double load_wait_time_s; // Time from connecting to starting measuring.
	bool connected;
	Timer settle_timer;      // Time since the scene became loaded.
	Timer measure_timer;     // Wall-clock time over the measured frames.

	std::vector<OpenGLEngine::GPUPassTimes> gpu_time_samples; // One per measured frame.
	std::vector<double> section_time_samples[OpenGLEngine::NUM_GPU_SECTIONS]; // [section][frame]
	std::vector<double> cpu_section_time_samples[OpenGLEngine::NUM_GPU_SECTIONS]; // [section][frame]
	std::vector<double> draw_CPU_time_samples; // One per measured frame.
	std::vector<OpenGLEngine::DrawCounts> draw_count_samples; // One per measured frame.
};
