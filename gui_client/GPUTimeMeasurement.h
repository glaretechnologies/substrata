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

Shader A/B mode (--ab_shader, see setShaderAB()): after loading once, alternates
two variants of a shader file, A and B, reloading the shaders each time, and
measures each variant several times in the same process.  This is much faster
than a client launch per measurement, and the alternation cancels out drift.
=====================================================================*/
class GPUTimeMeasurement
{
public:
	GPUTimeMeasurement(const std::string& output_path, const Vec3d& cam_pos, const Vec3d& cam_angles, int num_frames);

	// Enables the shader A/B mode: the contents of the files at variant_a_path and variant_b_path are alternately written to target_shader_path (a file in the
	// engine's shader directory), and the shaders reloaded, num_rounds times each, measuring num_frames frames each time.  The target file's original
	// contents are restored afterwards.  Needs a BUILD_TESTS build, for shader reloading.
	void setShaderAB(const std::string& target_shader_path, const std::string& variant_a_path, const std::string& variant_b_path, int num_rounds);

	// Call once per frame.  Returns true once the report has been written (or writing it failed), after which the client can exit.
	bool think(GUIClient& gui_client, OpenGLEngine& engine);

private:
	std::string makeReport(const OpenGLEngine& engine) const;
	std::string makeNearbyObjectsReport(OpenGLEngine& engine) const;
	void writeReport(const std::string& report);
	void saveRender(GUIClient& gui_client, OpenGLEngine& engine);
	void saveRender(GUIClient& gui_client, OpenGLEngine& engine, const std::string& path);
	bool thinkShaderAB(GUIClient& gui_client, OpenGLEngine& engine);
	std::string makeShaderABReport(const OpenGLEngine& engine) const;

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
	double wall_time_s;      // Wall-clock time over the measured frames.

	// Counting the fragment shader invocations in the opaque pass, to measure overdraw, is done over NUM_COUNTING_FRAMES frames after the timed frames.
	static const int NUM_COUNTING_FRAMES = 20;
	bool frag_invocation_counting_supported;
	int num_counting_frames_done;
	std::vector<uint64> opaque_frag_invocation_samples; // Counts read back during the counting frames.  Usually several per count, as each lags by a frame or more.

	// Shader A/B mode.  Samples alternate A, B, A, B, ...
	bool ab_mode;
	std::string ab_target_shader_path;
	std::string ab_original_contents;
	std::string ab_variant_contents[2];
	int ab_num_rounds;
	enum ABState { ABState_StartSample, ABState_WaitingForReload, ABState_Warmup, ABState_Measuring };
	ABState ab_state;
	int ab_sample_index;      // Index of the current sample.  Its variant is ab_sample_index % 2.
	int ab_frames_done;       // Warmup or measured frames done in the current state.
	Timer ab_timer;           // Time since the shader file was written for the current sample, or since measuring started.
	std::vector<OpenGLEngine::GPUPassTimes> ab_frame_times; // Per frame, for the current sample.
	struct ABSample
	{
		int variant;
		OpenGLEngine::GPUPassTimes median; // Per pass, the median over the sample's frames.
		double wall_time_per_frame_s;
		double reload_time_s;  // Time from writing the shader file to all programs being built.
	};
	std::vector<ABSample> ab_samples;
};
