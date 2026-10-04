/*=====================================================================
GPUTimeMeasurement.cpp
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GPUTimeMeasurement.h"


#include "GUIClient.h"
#include "UIInterface.h"
#include <graphics/PNGDecoder.h>
#include <graphics/ImageMap.h>
#include <utils/FileUtils.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <algorithm>
#include <limits>
#include <map>


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
	load_wait_time_s(0),
	connected(false),
	wall_time_s(0),
	frag_invocation_counting_supported(false),
	num_counting_frames_done(0)
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

		// Hold clouds, scripted objects etc. still, so that runs render the same thing, and their renders can be compared.
		gui_client.freeze_time = true;
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

		// Log what loading is waiting on now and then, so slow loading can be diagnosed.
		if(status_log_timer.elapsed() >= 1.0)
		{
			conPrint("GPUTimeMeasurement: " + doubleToStringNDecimalPlaces(total_timer.elapsed(), 1) + " s: " + gui_client.getSceneLoadingStatus());
			status_log_timer.reset();
		}

		const bool ready = became_loaded && (settle_timer.elapsed() >= SETTLE_S) && (total_timer.elapsed() >= MIN_WAIT_S);
		if(!ready)
		{
			if(total_timer.elapsed() < TIMEOUT_S)
				return false;
			load_timed_out = true;
			load_timeout_status = gui_client.getSceneLoadingStatus();
		}

		load_wait_time_s = total_timer.elapsed();

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

	if((int)gpu_time_samples.size() < num_frames)
	{
		gpu_time_samples.push_back(engine.getLastGPUPassTimes());
		draw_count_samples.push_back(engine.getLastDrawCounts());
		draw_CPU_time_samples.push_back(engine.getLastDrawCPUTime());
		for(int i=0; i<OpenGLEngine::NUM_GPU_SECTIONS; ++i)
		{
			section_time_samples[i].push_back(engine.getLastGPUSectionTime((OpenGLEngine::GPUSection)i));
			cpu_section_time_samples[i].push_back(engine.getLastCPUSectionTime((OpenGLEngine::GPUSection)i));
		}

		if((int)gpu_time_samples.size() < num_frames)
			return false;

		wall_time_s = measure_timer.elapsed();

		// Count the opaque pass's fragment shader invocations, for the overdraw, over some more frames after the timed ones, so the counting can't affect the times.
		frag_invocation_counting_supported = engine.setOpaqueFragInvocationCountingEnabled(true);
		if(frag_invocation_counting_supported)
			return false;
	}

	if(frag_invocation_counting_supported && (num_counting_frames_done < NUM_COUNTING_FRAMES))
	{
		num_counting_frames_done++;
		const uint64 count = engine.getLastDrawCounts().num_opaque_frag_invocations;
		if(count > 0)
			opaque_frag_invocation_samples.push_back(count);
		if(num_counting_frames_done < NUM_COUNTING_FRAMES)
			return false;
		engine.setOpaqueFragInvocationCountingEnabled(false);
	}

	if(!was_profiling_enabled)
		engine.setProfilingEnabled(false);

	writeReport(makeReport(engine) + makeNearbyObjectsReport(engine));
	saveRender(gui_client, engine);
	return true;
}


// Saves an image of the measured view next to the report, so runs can be checked visually to be measuring the same thing (e.g. that the same objects and textures had loaded).
void GPUTimeMeasurement::saveRender(GUIClient& gui_client, OpenGLEngine& engine)
{
	const std::string path = ::removeDotAndExtension(output_path) + ".png";
	try
	{
		const bool old_draw_overlay_objects = engine.getCurrentScene()->draw_overlay_objects;
		engine.getCurrentScene()->draw_overlay_objects = false; // Hide UI

		gui_client.ui_interface->setGLWidgetContextAsCurrent();
		ImageMapUInt8Ref map = engine.drawToBufferAndReturnImageMap();

		engine.getCurrentScene()->draw_overlay_objects = old_draw_overlay_objects;

		if(map->hasAlphaChannel())
			map = map->extract3ChannelImage();
		PNGDecoder::write(*map, path);
		conPrint("GPUTimeMeasurement: wrote render to '" + path + "'.");
	}
	catch(glare::Exception& e)
	{
		conPrint("GPUTimeMeasurement: failed to write render to '" + path + "': " + e.what());
	}
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


std::string GPUTimeMeasurement::makeReport(const OpenGLEngine& engine) const
{
	const std::vector<OpenGLEngine::GPUPassTimes>& samples = gpu_time_samples;
	const size_t n = samples.size();

	std::string s = "Measured " + toString(n) + " frames at " + toString(engine.getViewPortWidth()) + " x " + toString(engine.getViewPortHeight()) +
		" on " + engine.opengl_renderer + "\n";
	s += "Camera: pos (" + doubleToStringNDecimalPlaces(cam_pos.x, 3) + ", " + doubleToStringNDecimalPlaces(cam_pos.y, 3) + ", " + doubleToStringNDecimalPlaces(cam_pos.z, 3) +
		"), heading " + doubleToStringNDecimalPlaces(cam_angles.x, 4) + ", pitch " + doubleToStringNDecimalPlaces(cam_angles.y, 4) + "\n";
	s += "Waited " + doubleToStringNDecimalPlaces(load_wait_time_s, 1) + " s after connecting for the scene to load\n";
	if(load_timed_out)
		s += "WARNING: the scene had not finished loading when measurement started, so these times may not be comparable with other runs.  Still waiting on: " + load_timeout_status + "\n";
	s += "Wall-clock time per frame: " + doubleToStringNDecimalPlaces(wall_time_s / n * 1.0e3, 3) + " ms (vsync is off while measuring)\n\n";

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

	// If this is close to the wall-clock frame time and above the GPU total, the CPU is the bottleneck.  If the GPU is, this includes time blocked waiting for it.
	std::vector<double> draw_CPU_times = draw_CPU_time_samples;
	s += statsRow("CPU draw()", draw_CPU_times);

	// Unlike the pass timers above, the sections are timed every frame and cover all of draw(), so they include the work outside the timed passes.
	// The CPU column is the CPU time spent issuing each section's commands.  If it's about the same as the GPU time, the GPU is probably waiting for the CPU to submit work.
	s += "\n" + ::rightSpacePad("draw() section (ms)", 28) + ::leftPad("mean", ' ', 9) + ::leftPad("median", ' ', 9) + ::leftPad("p90", ' ', 9) + ::leftPad("max", ' ', 9) + ::leftPad("CPU mean", ' ', 10) + "\n";
	for(int i=0; i<OpenGLEngine::NUM_GPU_SECTIONS; ++i)
	{
		std::vector<double> section_times = section_time_samples[i];
		std::string row = statsRow(OpenGLEngine::getGPUSectionName((OpenGLEngine::GPUSection)i), section_times, /*name_width=*/28);
		row.pop_back(); // Remove newline

		double cpu_sum = 0;
		for(size_t z=0; z<cpu_section_time_samples[i].size(); ++z)
			cpu_sum += cpu_section_time_samples[i][z];
		s += row + ::leftPad(doubleToStringNDecimalPlaces(cpu_sum / cpu_section_time_samples[i].size() * 1.0e3, 3), ' ', 10) + "\n";
	}

	// Main-pass draw counts barely change between frames from a fixed camera, so the last frame's are representative.
	const OpenGLEngine::DrawCounts& c = draw_count_samples.back();
	s += "\nObjects in view frustum: " + toString(c.num_obs_in_frustum) + "\n";
	s += "Main pass: " + toString(c.num_batches_drawn) + " batches, " + uInt32ToStringCommaSeparated(c.num_tris_drawn) + " tris, " + toString(c.num_prog_changes) + " program changes\n";
	s += "Multi-draw-indirect calls in the frame (all passes): " + toString(c.num_multi_draw_indirect_calls) + "\n";
	s += "Phong uniform buffer updates in the frame (all passes): " + toString(c.num_phong_uniform_buf_updates) + ", of which skipped as unchanged: " + toString(c.num_phong_uniform_buf_updates_skipped) + "\n";
	s += "Phong texture sets bound in the frame (all passes): " + toString(c.num_phong_texture_sets_bound) + ", of which all already bound: " + toString(c.num_phong_texture_sets_already_bound) + "\n";
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

	// The overdraw: fragment shader invocations per pixel in the opaque pass.  1 would mean each pixel is shaded once.  Fragments rejected by early depth testing
	// aren't shaded, so aren't counted.  With MSAA, the shader runs once per pixel per primitive, not per sample.
	if(!frag_invocation_counting_supported)
		s += "Opaque pass overdraw: not measured, as pipeline statistics queries aren't supported\n";
	else if(opaque_frag_invocation_samples.empty())
		s += "Opaque pass overdraw: not measured, as no counts were read back\n";
	else
	{
		std::vector<uint64> counts = opaque_frag_invocation_samples;
		std::sort(counts.begin(), counts.end());
		const uint64 median_count = counts[counts.size() / 2];
		const double num_pixels = (double)engine.getViewPortWidth() * engine.getViewPortHeight();
		s += "Opaque pass fragment shader invocations: " + uInt64ToStringCommaSeparated(median_count) + " (median of " + toString(counts.size()) + " counts, min " +
			uInt64ToStringCommaSeparated(counts.front()) + ", max " + uInt64ToStringCommaSeparated(counts.back()) + "), overdraw " +
			doubleToStringNDecimalPlaces(median_count / num_pixels, 2) + " per pixel\n";
	}

	return s;
}


static std::string glTypeName(GLenum type)
{
	switch(type)
	{
	case 0x1400: return "byte";
	case 0x1401: return "ubyte";
	case 0x1402: return "short";
	case 0x1403: return "ushort";
	case 0x1405: return "uint";
	case 0x1406: return "float";
	case 0x140B: return "half";
	case 0x8D9F: return "int_2_10_10_10";
	default: return "0x" + toHexString(type);
	}
}


// Does the AABB intersect the view frustum?  The same test as AABBIntersectsFrustum() in OpenGLEngine.cpp.
static bool AABBInFrustum(const OpenGLScene& scene, const js::AABBox& aabb)
{
	for(int z=0; z<scene.num_frustum_clip_planes; ++z)
	{
		const Vec4f normal = scene.frustum_clip_planes[z].getNormal();
		float min_dist = std::numeric_limits<float>::infinity();
		for(int i=0; i<8; ++i)
		{
			const Vec4f corner((i & 1) ? aabb.max_[0] : aabb.min_[0], (i & 2) ? aabb.max_[1] : aabb.min_[1], (i & 4) ? aabb.max_[2] : aabb.min_[2], 0.f);
			min_dist = myMin(min_dist, dot(normal, corner));
		}
		if(min_dist >= scene.frustum_clip_planes[z].getD())
			return false;
	}
	return true;
}


// Lists the objects drawn by the colour and depth pre-pass: those in the view frustum, within 80 m of the camera (see OpenGLEngine::drawColourAndDepthPrePass()),
// with the most triangles first, so that expensive objects can be identified.
std::string GPUTimeMeasurement::makeNearbyObjectsReport(OpenGLEngine& engine) const
{
	const float PREPASS_MAX_DIST = 80.f;
	OpenGLScene& scene = *engine.getCurrentScene();
	const Vec4f campos((float)cam_pos.x, (float)cam_pos.y, (float)cam_pos.z, 1.f);

	struct ObInfo
	{
		const GLObject* ob;
		uint64 num_tris; // Including instances
	};
	std::vector<ObInfo> obs;
	std::map<std::string, std::pair<uint64, uint64>> batches_and_tris_per_prog;
	uint64 total_batches = 0, total_tris = 0;

	for(size_t i=0; i<scene.objects.vector.size(); ++i)
	{
		const GLObject* ob = scene.objects.vector[i].ptr();
		if(ob->mesh_data.isNull() || !AABBInFrustum(scene, ob->aabb_ws) || (ob->aabb_ws.distanceToPoint(campos) > PREPASS_MAX_DIST))
			continue;

		const uint64 num_instances = ob->instance_matrix_vbo.nonNull() ? (uint64)myMax(1, ob->num_instances_to_draw) : 1;
		const uint64 num_tris = (uint64)ob->mesh_data->getNumTris() * num_instances;
		obs.push_back(ObInfo({ ob, num_tris }));
		total_batches += ob->batch_draw_info.size();
		total_tris += num_tris;

		const ArrayRef<OpenGLBatch> batches = ob->getUsedBatches();
		for(size_t z=0; z<batches.size(); ++z)
		{
			const uint32 mat_index = batches[z].material_index;
			const std::string prog_name = (mat_index < ob->materials.size() && ob->materials[mat_index].shader_prog.nonNull()) ? ob->materials[mat_index].shader_prog->prog_name : "?";
			batches_and_tris_per_prog[prog_name].first++;
			batches_and_tris_per_prog[prog_name].second += (uint64)(batches[z].num_indices / 3) * num_instances;
		}
	}

	std::sort(obs.begin(), obs.end(), [](const ObInfo& a, const ObInfo& b) { return a.num_tris > b.num_tris; });

	std::string s = "\nObjects in the view frustum within " + toString((int)PREPASS_MAX_DIST) + " m of the camera (drawn by the pre-pass): " + toString(obs.size()) + ", " +
		uInt64ToStringCommaSeparated(total_batches) + " batches, " + uInt64ToStringCommaSeparated(total_tris) + " tris\n";

	s += "\nBy shader program:\n";
	for(auto it = batches_and_tris_per_prog.begin(); it != batches_and_tris_per_prog.end(); ++it)
		s += "  " + ::rightSpacePad(it->first, 60) + ::leftPad(uInt64ToStringCommaSeparated(it->second.first), ' ', 8) + " batches " + ::leftPad(uInt64ToStringCommaSeparated(it->second.second), ' ', 12) + " tris\n";

	const size_t MAX_OBS_LISTED = 40;
	s += "\nObjects with the most triangles:\n";
	for(size_t i=0; i<myMin(obs.size(), MAX_OBS_LISTED); ++i)
	{
		const GLObject* ob = obs[i].ob;
		const OpenGLMeshRenderData& mesh = *ob->mesh_data;
		const Vec4f centre = ob->aabb_ws.centroid();
		const Vec4f size = ob->aabb_ws.max_ - ob->aabb_ws.min_;
		const VertexAttrib* pos_attr = mesh.vertex_spec.attributes.empty() ? nullptr : &mesh.vertex_spec.attributes[0];

		s += "  " + ::leftPad(uInt64ToStringCommaSeparated(obs[i].num_tris), ' ', 10) + " tris, " + uInt64ToStringCommaSeparated(mesh.getNumVerts()) + " verts, " +
			toString(ob->batch_draw_info.size()) + " batches, " +
			(ob->instance_matrix_vbo.nonNull() ? (toString(ob->num_instances_to_draw) + " instances, ") : std::string()) +
			"indices: " + toString(mesh.getIndexTypeSize()) + " B, vert stride: " + toString(mesh.vertex_spec.vertStride()) + " B, pos: " +
			(pos_attr ? (toString(pos_attr->num_comps) + " x " + glTypeName(pos_attr->type)) : std::string("?")) +
			", centre (" + doubleToStringNDecimalPlaces(centre[0], 1) + ", " + doubleToStringNDecimalPlaces(centre[1], 1) + ", " + doubleToStringNDecimalPlaces(centre[2], 1) + ")" +
			", size (" + doubleToStringNDecimalPlaces(size[0], 1) + ", " + doubleToStringNDecimalPlaces(size[1], 1) + ", " + doubleToStringNDecimalPlaces(size[2], 1) + ")" +
			", prog: " + ((!ob->materials.empty() && ob->materials[0].shader_prog.nonNull()) ? ob->materials[0].shader_prog->prog_name : std::string("?")) + "\n";
	}

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
