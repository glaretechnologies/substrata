/*=====================================================================
XRSession.h
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../maths/Matrix4f.h"
#include "../maths/vec2.h"
#include <cstdint>
#include <string>


class CameraController;
class OpenGLEngine;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

struct XRMirrorView
{
	XRMirrorView()
	:	valid(false),
		world_to_camera_space_matrix(Matrix4f::identity()),
		sensor_width(0.f),
		lens_sensor_dist(0.f),
		render_aspect_ratio(0.f),
		lens_shift_up(0.f),
		lens_shift_right(0.f),
		projection_matrix_override_valid(false),
		projection_matrix_override(Matrix4f::identity())
	{}

	bool valid;
	Matrix4f world_to_camera_space_matrix;
	float sensor_width;
	float lens_sensor_dist;
	float render_aspect_ratio;
	float lens_shift_up;
	float lens_shift_right;
	bool projection_matrix_override_valid;
	Matrix4f projection_matrix_override;
};


struct XRTrackedPoseState
{
	XRTrackedPoseState()
	:	active(false),
		position_valid(false),
		orientation_valid(false),
		position_tracked(false),
		orientation_tracked(false),
		object_to_world_matrix(Matrix4f::identity())
	{}

	bool active;
	bool position_valid;
	bool orientation_valid;
	bool position_tracked;
	bool orientation_tracked;
	Matrix4f object_to_world_matrix;
};


struct XRHandInputState
{
	XRHandInputState()
	:	subaction_path_valid(false),
		interaction_profile_valid(false),
		select_active(false),
		select_pressed(false),
		trigger_active(false),
		trigger_value(0.f),
		move2d_active(false),
		move2d_value(0.f, 0.f)
	{}

	bool subaction_path_valid;
	bool interaction_profile_valid;
	bool select_active;
	bool select_pressed;
	bool trigger_active;
	float trigger_value;
	bool move2d_active;
	Vec2f move2d_value;
	std::string interaction_profile;
	XRTrackedPoseState grip_pose;
	XRTrackedPoseState aim_pose;
};


struct XREyeViewState
{
	XREyeViewState()
	:	fov_valid(false),
		angle_left_deg(0.f),
		angle_right_deg(0.f),
		angle_up_deg(0.f),
		angle_down_deg(0.f)
	{}

	XRTrackedPoseState raw_pose;
	XRTrackedPoseState world_pose;
	bool fov_valid;
	float angle_left_deg;
	float angle_right_deg;
	float angle_up_deg;
	float angle_down_deg;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif


struct XRRuntimeProbeResult
{
	bool xr_compiled_in;
	bool loader_reachable;
	bool instance_extensions_enumerated;
	bool opengl_enable_extension_available;
	bool instance_created;
	bool system_available;
	bool system_properties_queried;
	bool graphics_requirements_obtained;
	bool graphics_requirements_compatible;
	bool session_created;
	bool local_reference_space_created;
	bool view_configuration_enumerated;
	bool environment_blend_mode_selected;
	bool swapchains_created;
	bool session_running;
	bool frame_loop_active;
	bool views_located;
	bool should_render_current_frame;
	bool projection_layer_submitted;
	bool action_set_created;
	bool pose_actions_created;
	bool input_actions_created;
	bool action_spaces_created;
	bool action_bindings_suggested;
	bool action_sets_attached;
	bool actions_synced;
	uint32_t configured_view_count;
	uint32_t located_view_count;
	uint32_t suggested_binding_profile_count;
	std::string backend_name;
	std::string message;
	std::string actions_message;
	std::string runtime_name;
	std::string runtime_version_string;
	std::string system_name;
	std::string session_state_string;
	std::string environment_blend_mode_string;
	std::string reference_space_type_string;
	std::string swapchain_format_string;
	std::string graphics_api_version_string;
	std::string graphics_api_min_version_string;
	std::string graphics_api_max_version_string;
};


class XRSession
{
public:
	XRSession();
	~XRSession();

	static XRRuntimeProbeResult probeRuntime();

	bool initialiseForCurrentOpenGLContext();
	void renderFrame(OpenGLEngine& opengl_engine, const CameraController& cam_controller, float near_draw_dist, float max_draw_dist);
	bool isInitialised() const;

	const XRRuntimeProbeResult& getLastResult() const { return last_result; }
	const XRMirrorView& getMirrorView() const { return last_mirror_view; }
	const XRTrackedPoseState& getHeadPoseState() const { return head_pose_state; }
	const XRTrackedPoseState& getRawHeadPoseState() const { return raw_head_pose_state; }
	const XREyeViewState& getLeftEyeViewState() const { return left_eye_view_state; }
	const XREyeViewState& getRightEyeViewState() const { return right_eye_view_state; }
	const XRHandInputState& getLeftHandState() const { return left_hand_state; }
	const XRHandInputState& getRightHandState() const { return right_hand_state; }

	void requestRecenter();
	void shutdown();

private:
	XRRuntimeProbeResult last_result;
	XRMirrorView last_mirror_view;
	XRTrackedPoseState head_pose_state;
	XRTrackedPoseState raw_head_pose_state;
	XREyeViewState left_eye_view_state;
	XREyeViewState right_eye_view_state;
	XRHandInputState left_hand_state;
	XRHandInputState right_hand_state;

#if defined(XR_SUPPORT)
	struct OpaqueState;
	OpaqueState* state;
#endif
};
