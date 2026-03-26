/*=====================================================================
XRSession.cpp
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "XRSession.h"


#include "CameraController.h"
#include "../maths/Quat.h"
#include "../opengl/FrameBuffer.h"
#include "../opengl/OpenGLEngine.h"
#include "../opengl/RenderBuffer.h"
#include "../utils/Clock.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>


#if defined(XR_SUPPORT)
	#if defined(_WIN32)
		#include "../utils/IncludeWindows.h"
		#include <Unknwn.h>
		#define XR_USE_PLATFORM_WIN32
		#define XR_USE_GRAPHICS_API_OPENGL
	#endif
	#include <openxr/openxr.h>
	#if defined(_WIN32)
		#include <openxr/openxr_platform.h>
		#include <opengl/IncludeOpenGL.h>
	#endif
#endif


namespace
{
static XRRuntimeProbeResult makeDefaultResult()
{
	XRRuntimeProbeResult result;
	result.xr_compiled_in = false;
	result.loader_reachable = false;
	result.instance_extensions_enumerated = false;
	result.opengl_enable_extension_available = false;
	result.instance_created = false;
	result.system_available = false;
	result.system_properties_queried = false;
	result.graphics_requirements_obtained = false;
	result.graphics_requirements_compatible = false;
	result.session_created = false;
	result.local_reference_space_created = false;
	result.view_configuration_enumerated = false;
	result.environment_blend_mode_selected = false;
	result.swapchains_created = false;
	result.session_running = false;
	result.frame_loop_active = false;
	result.views_located = false;
	result.should_render_current_frame = false;
	result.projection_layer_submitted = false;
	result.action_set_created = false;
	result.pose_actions_created = false;
	result.input_actions_created = false;
	result.action_spaces_created = false;
	result.action_bindings_suggested = false;
	result.action_sets_attached = false;
	result.actions_synced = false;
	result.configured_view_count = 0;
	result.located_view_count = 0;
	result.suggested_binding_profile_count = 0;
	result.backend_name = "None";
	result.message = "XR support is not compiled into this build.";
	result.actions_message.clear();
	result.session_state_string = "UNKNOWN";
	result.reference_space_type_string.clear();
	return result;
}


#if defined(XR_SUPPORT)
static std::string makeVersionString(uint64_t version)
{
	return std::to_string(XR_VERSION_MAJOR(version)) + "." + std::to_string(XR_VERSION_MINOR(version)) + "." + std::to_string(XR_VERSION_PATCH(version));
}


static std::string sessionStateString(XrSessionState state)
{
	switch(state)
	{
	case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: return "UNHANDLED";
	}
}


static std::string environmentBlendModeString(XrEnvironmentBlendMode mode)
{
	switch(mode)
	{
	case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
	case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
	case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
	default: return "UNKNOWN";
	}
}


static std::string referenceSpaceTypeString(XrReferenceSpaceType type)
{
	switch(type)
	{
	case XR_REFERENCE_SPACE_TYPE_VIEW:	return "VIEW";
	case XR_REFERENCE_SPACE_TYPE_LOCAL:	return "LOCAL";
	case XR_REFERENCE_SPACE_TYPE_STAGE:	return "STAGE";
	default:							return "UNKNOWN";
	}
}


static std::string swapchainFormatString(int64_t format)
{
#if defined(_WIN32)
	switch((GLenum)format)
	{
	case GL_RGBA8: return "GL_RGBA8";
	case GL_RGBA16: return "GL_RGBA16";
	case GL_RGBA16F: return "GL_RGBA16F";
	case GL_RGB16F: return "GL_RGB16F";
	case GL_RGB10_A2: return "GL_RGB10_A2";
#ifdef GL_SRGB8
	case GL_SRGB8: return "GL_SRGB8";
#endif
#ifdef GL_SRGB8_ALPHA8
	case GL_SRGB8_ALPHA8: return "GL_SRGB8_ALPHA8";
#endif
	default: return "GL_FORMAT_" + std::to_string((long long)format);
	}
#else
	return std::to_string((long long)format);
#endif
}


static std::string swapchainFormatListString(const std::vector<int64_t>& runtime_formats)
{
	std::string s;
	for(size_t i=0; i<runtime_formats.size(); ++i)
	{
		if(i > 0)
			s += ", ";
		s += swapchainFormatString(runtime_formats[i]);
	}
	return s;
}


static bool chooseSwapchainFormat(const std::vector<int64_t>& runtime_formats, int64_t& chosen_format_out)
{
#if defined(_WIN32)
	static const int64_t preferred_formats[] = {
		(int64_t)GL_RGBA8,
		(int64_t)GL_RGB10_A2,
		(int64_t)GL_RGBA16,
#ifdef GL_SRGB8_ALPHA8
		(int64_t)GL_SRGB8_ALPHA8,
#endif
		// Keep sRGB/half-float as later fallbacks. Some SteamVR/VIVE compositor paths accept the
		// swapchain creation call but later reject submitted projection layers for these formats.
		(int64_t)GL_RGBA16F,
	};

	for(size_t i=0; i<sizeof(preferred_formats) / sizeof(preferred_formats[0]); ++i)
	{
		for(size_t z=0; z<runtime_formats.size(); ++z)
		{
			if(runtime_formats[z] == preferred_formats[i])
			{
				chosen_format_out = preferred_formats[i];
				return true;
			}
		}
	}
#endif

	return false;
}


static void clampRecommendedSwapchainSizeForRuntime(const XRRuntimeProbeResult& result, uint32_t& width, uint32_t& height)
{
	if(result.runtime_name.find("SteamVR/OpenXR") == std::string::npos)
		return;

	const uint32_t max_dim = myMax(width, height);
	// VIVE Business Streaming + SteamVR can recommend eye sizes that are a bit too ambitious for stable head-turn latency.
	// Clamp more aggressively so the compositor has enough headroom and doesn't expose black reprojection edges on quick turns.
	static const uint32_t steamvr_safe_max_dim = 2200u;
	if(max_dim <= steamvr_safe_max_dim)
		return;

	const double scale = (double)steamvr_safe_max_dim / (double)max_dim;
	width = myMax(1u, (uint32_t)std::floor((double)width * scale));
	height = myMax(1u, (uint32_t)std::floor((double)height * scale));
}


static Vec3f mapXRVectorToEngineSpace(const Vec4f& xr_vector)
{
	return Vec3f(xr_vector[0], -xr_vector[2], xr_vector[1]);
}


static Vec3f mapXRPositionToEngineSpace(const XrVector3f& position)
{
	return Vec3f(position.x, -position.z, position.y);
}


static Quatf makeQuatf(const XrQuaternionf& orientation)
{
	return normalise(Quatf(orientation.x, orientation.y, orientation.z, orientation.w));
}


static Vec3f rotateAroundWorldUp(const Vec3f& v, float yaw_angle)
{
	return toVec3f(Quatf::zAxisRot(yaw_angle).rotateVector(v.toVec4fVector()));
}


static Vec3f getXRForwardVectorInEngineSpace(const XrQuaternionf& orientation)
{
	const Quatf q = makeQuatf(orientation);
	return mapXRVectorToEngineSpace(q.rotateVector(Vec4f(0, 0, -1, 0)));
}


static Quatf buildEngineSpaceQuatFromBasis(const Vec3f& right_ws, const Vec3f& forwards_ws, const Vec3f& up_ws)
{
	return normalise(Quatf::fromMatrix(Matrix4f(
		right_ws.toVec4fVector(),
		forwards_ws.toVec4fVector(),
		up_ws.toVec4fVector(),
		Vec4f(0, 0, 0, 1)
	)));
}


static Quatf buildEngineSpaceQuatFromXROrientation(const XrQuaternionf& orientation)
{
	const Quatf xr_orientation = makeQuatf(orientation);
	return buildEngineSpaceQuatFromBasis(
		mapXRVectorToEngineSpace(xr_orientation.rotateVector(Vec4f(1, 0, 0, 0))),
		mapXRVectorToEngineSpace(xr_orientation.rotateVector(Vec4f(0, 0, -1, 0))),
		mapXRVectorToEngineSpace(xr_orientation.rotateVector(Vec4f(0, 1, 0, 0)))
	);
}


static Quatf buildNeutralWorldHeadOrientation(float world_heading)
{
	Vec3d right_ws_d, up_ws_d, forward_ws_d;
	CameraController::getBasisForAngles(Vec3d(world_heading, Maths::pi<double>() * 0.5, 0.0), Vec3d(0, 0, 1), right_ws_d, up_ws_d, forward_ws_d);
	return buildEngineSpaceQuatFromBasis(toVec3f(right_ws_d), toVec3f(forward_ws_d), toVec3f(up_ws_d));
}


static bool computeProjectionParamsFromFOV(const XrFovf& fov, float& sensor_width_out, float& lens_sensor_dist_out, float& render_aspect_ratio_out,
	float& lens_shift_up_out, float& lens_shift_right_out)
{
	const float tan_left = std::tan(fov.angleLeft);
	const float tan_right = std::tan(fov.angleRight);
	const float tan_down = std::tan(fov.angleDown);
	const float tan_up = std::tan(fov.angleUp);

	const float half_width = 0.5f * (tan_right - tan_left);
	const float half_height = 0.5f * (tan_up - tan_down);

	if(!(half_width > 0.f) || !(half_height > 0.f))
		return false;

	lens_sensor_dist_out = 1.f;
	sensor_width_out = 2.f * half_width * lens_sensor_dist_out;
	render_aspect_ratio_out = half_width / half_height;
	lens_shift_right_out = 0.5f * (tan_right + tan_left) * lens_sensor_dist_out;
	lens_shift_up_out = 0.5f * (tan_up + tan_down) * lens_sensor_dist_out;
	return isFinite(sensor_width_out) && isFinite(render_aspect_ratio_out) && isFinite(lens_shift_up_out) && isFinite(lens_shift_right_out);
}


static bool buildOpenGLProjectionMatrixFromFOV(const XrFovf& fov, float near_z, Matrix4f& projection_matrix_out)
{
	if(!(near_z > 0.f))
		return false;

	const float tan_left = std::tan(fov.angleLeft);
	const float tan_right = std::tan(fov.angleRight);
	const float tan_down = std::tan(fov.angleDown);
	const float tan_up = std::tan(fov.angleUp);

	const float tan_width = tan_right - tan_left;
	const float tan_height = tan_up - tan_down;
	if(!(tan_width > 0.f) || !(tan_height > 0.f))
		return false;

	const float matrix_data[16] = {
		2.f / tan_width,                     0.f,                              0.f,            0.f,
		0.f,                                2.f / tan_height,                 0.f,            0.f,
		(tan_right + tan_left) / tan_width, (tan_up + tan_down) / tan_height, -1.f,           -1.f,
		0.f,                                0.f,                              -2.f * near_z,  0.f
	};

	projection_matrix_out = Matrix4f(matrix_data);
	return true;
}


static void clearOpenGLErrors()
{
#if defined(_WIN32)
	while(glGetError() != GL_NO_ERROR)
	{}
#endif
}


static bool buildCalibrationHeadPose(const std::vector<XrView>& views, uint32_t view_count, XrPosef& head_pose_out)
{
	if(view_count == 0)
		return false;

	head_pose_out.orientation = views[0].pose.orientation;
	head_pose_out.position = views[0].pose.position;

	if(view_count >= 2)
	{
		const XrVector3f& a = views[0].pose.position;
		const XrVector3f& b = views[1].pose.position;
		head_pose_out.position.x = 0.5f * (a.x + b.x);
		head_pose_out.position.y = 0.5f * (a.y + b.y);
		head_pose_out.position.z = 0.5f * (a.z + b.z);
	}

	return true;
}


static float orientationAngleDelta(const XrQuaternionf& a, const XrQuaternionf& b)
{
	const float abs_dot = myClamp(std::fabs(dotProduct(makeQuatf(a), makeQuatf(b))), 0.f, 1.f);
	return 2.f * std::acos(abs_dot);
}


static float positionDistance(const XrVector3f& a, const XrVector3f& b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}


static void buildWorldToCameraMatrixFromXRPose(const XrPosef& pose, const Quatf& orientation_offset, float position_yaw_offset, const Vec3f& world_translation, Matrix4f& world_to_camera_out)
{
	const Quatf world_orientation = normalise(orientation_offset * buildEngineSpaceQuatFromXROrientation(pose.orientation));
	const Vec3f right_ws = toVec3f(world_orientation.rotateVector(Vec4f(1, 0, 0, 0)));
	const Vec3f forwards_ws = toVec3f(world_orientation.rotateVector(Vec4f(0, 1, 0, 0)));
	const Vec3f up_ws = toVec3f(world_orientation.rotateVector(Vec4f(0, 0, 1, 0)));
	const Vec3f world_pos = world_translation + rotateAroundWorldUp(mapXRPositionToEngineSpace(pose.position), position_yaw_offset);

	const Matrix4f camera_rot = Matrix4f::fromRows(right_ws.toVec4fVector(), forwards_ws.toVec4fVector(), up_ws.toVec4fVector(), Vec4f(0, 0, 0, 1));
	camera_rot.rightMultiplyWithTranslationMatrix(-world_pos.toVec4fVector(), world_to_camera_out);
}


static void buildObjectToWorldMatrixFromXRPose(const XrPosef& pose, const Quatf& orientation_offset, float position_yaw_offset, const Vec3f& world_translation, Matrix4f& object_to_world_out)
{
	const Quatf world_orientation = normalise(orientation_offset * buildEngineSpaceQuatFromXROrientation(pose.orientation));
	const Vec3f right_ws = toVec3f(world_orientation.rotateVector(Vec4f(1, 0, 0, 0)));
	const Vec3f forwards_ws = toVec3f(world_orientation.rotateVector(Vec4f(0, 1, 0, 0)));
	const Vec3f up_ws = toVec3f(world_orientation.rotateVector(Vec4f(0, 0, 1, 0)));
	const Vec3f world_pos = world_translation + rotateAroundWorldUp(mapXRPositionToEngineSpace(pose.position), position_yaw_offset);
	object_to_world_out = Matrix4f(right_ws.toVec4fVector(), forwards_ws.toVec4fVector(), up_ws.toVec4fVector(), world_pos.toVec4fPoint());
}


static void resetTrackedPoseState(XRTrackedPoseState& pose_state)
{
	pose_state = XRTrackedPoseState();
}


static void resetEyeViewState(XREyeViewState& eye_state)
{
	eye_state = XREyeViewState();
}


static void resetHandInputState(XRHandInputState& hand_state)
{
	hand_state = XRHandInputState();
}


static void copyCStringTruncated(char* dest, size_t dest_size, const std::string& src)
{
	if(dest_size == 0)
		return;

	const size_t copy_size = std::min(src.size(), dest_size - 1);
	std::memcpy(dest, src.data(), copy_size);
	dest[copy_size] = '\0';
}


static void resetEventDataBuffer(XrEventDataBuffer& event)
{
	std::memset(&event, 0, sizeof(XrEventDataBuffer));
	event.type = XR_TYPE_EVENT_DATA_BUFFER;
}


static bool hasExtension(const std::vector<XrExtensionProperties>& properties, const char* extension_name)
{
	for(size_t i=0; i<properties.size(); ++i)
		if(std::strcmp(properties[i].extensionName, extension_name) == 0)
			return true;

	return false;
}


static bool chooseEnvironmentBlendMode(const std::vector<XrEnvironmentBlendMode>& available_modes, XrEnvironmentBlendMode& chosen_mode_out)
{
	static const XrEnvironmentBlendMode preferred_modes[] = {
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
		XR_ENVIRONMENT_BLEND_MODE_ADDITIVE
	};

	for(size_t i=0; i<sizeof(preferred_modes) / sizeof(preferred_modes[0]); ++i)
	{
		for(size_t z=0; z<available_modes.size(); ++z)
		{
			if(available_modes[z] == preferred_modes[i])
			{
				chosen_mode_out = preferred_modes[i];
				return true;
			}
		}
	}

	return false;
}


static bool chooseReferenceSpaceType(const std::vector<XrReferenceSpaceType>& available_types, XrReferenceSpaceType& chosen_type_out)
{
	static const XrReferenceSpaceType preferred_types[] = {
		XR_REFERENCE_SPACE_TYPE_STAGE,
		XR_REFERENCE_SPACE_TYPE_LOCAL,
		XR_REFERENCE_SPACE_TYPE_VIEW
	};

	for(size_t i=0; i<sizeof(preferred_types) / sizeof(preferred_types[0]); ++i)
	{
		for(size_t z=0; z<available_types.size(); ++z)
		{
			if(available_types[z] == preferred_types[i])
			{
				chosen_type_out = preferred_types[i];
				return true;
			}
		}
	}

	return false;
}


static bool xrStringToPathChecked(XrInstance instance, const char* path_string, XrPath& path_out, std::string& error_out)
{
	const XrResult path_res = xrStringToPath(instance, path_string, &path_out);
	if(XR_FAILED(path_res))
	{
		error_out = "xrStringToPath('" + std::string(path_string) + "') failed with XrResult=" + std::to_string((int)path_res);
		return false;
	}

	return true;
}


static std::string xrPathToStringSafe(XrInstance instance, XrPath path)
{
	if(path == XR_NULL_PATH)
		return std::string();

	uint32_t path_string_count = 0;
	const XrResult count_res = xrPathToString(instance, path, 0, &path_string_count, NULL);
	if(XR_FAILED(count_res) || path_string_count == 0)
		return std::string();

	std::vector<char> path_chars(path_string_count);
	const XrResult fill_res = xrPathToString(instance, path, path_string_count, &path_string_count, &path_chars[0]);
	if(XR_FAILED(fill_res) || path_string_count == 0)
		return std::string();

	return std::string(&path_chars[0]);
}


static bool sessionStateAllowsFrameLoop(XrSessionState state, bool session_running)
{
	if(!session_running)
		return false;

	return
		(state == XR_SESSION_STATE_READY) ||
		(state == XR_SESSION_STATE_SYNCHRONIZED) ||
		(state == XR_SESSION_STATE_VISIBLE) ||
		(state == XR_SESSION_STATE_FOCUSED);
}


static bool initialiseInstanceAndSystem(XRRuntimeProbeResult& result, bool enable_opengl_extension, XrInstance& instance_out, XrSystemId& system_id_out)
{
	result.xr_compiled_in = true;
	result.backend_name = "OpenXR";
	result.message = "OpenXR loader is present, but runtime probing did not complete.";

	uint32_t extension_count = 0;
	const XrResult enum_res = xrEnumerateInstanceExtensionProperties(NULL, 0, &extension_count, NULL);
	if(XR_FAILED(enum_res))
	{
		result.message = "xrEnumerateInstanceExtensionProperties failed with XrResult=" + std::to_string((int)enum_res);
		return false;
	}

	result.loader_reachable = true;
	result.instance_extensions_enumerated = true;

	std::vector<XrExtensionProperties> properties(extension_count);
	for(size_t i=0; i<properties.size(); ++i)
		properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;

	if(extension_count > 0)
	{
		const XrResult enum_fill_res = xrEnumerateInstanceExtensionProperties(NULL, extension_count, &extension_count, &properties[0]);
		if(XR_FAILED(enum_fill_res))
		{
			result.message = "xrEnumerateInstanceExtensionProperties(fill) failed with XrResult=" + std::to_string((int)enum_fill_res);
			return false;
		}
	}

#if defined(_WIN32)
	result.opengl_enable_extension_available = hasExtension(properties, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
#endif

	if(enable_opengl_extension && !result.opengl_enable_extension_available)
	{
		result.message = "OpenXR runtime is available, but XR_KHR_opengl_enable is not advertised by the loader/runtime.";
		return false;
	}

	std::vector<const char*> enabled_extensions;
	if(enable_opengl_extension)
		enabled_extensions.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);

	XrInstanceCreateInfo create_info = { XR_TYPE_INSTANCE_CREATE_INFO };
	create_info.createFlags = 0;
	copyCStringTruncated(create_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Metasiberia");
	copyCStringTruncated(create_info.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Substrata");
	create_info.applicationInfo.applicationVersion = 1;
	create_info.applicationInfo.engineVersion = 1;
	create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	create_info.enabledExtensionCount = (uint32_t)enabled_extensions.size();
	create_info.enabledExtensionNames = enabled_extensions.empty() ? NULL : &enabled_extensions[0];

	const XrResult create_res = xrCreateInstance(&create_info, &instance_out);
	if(XR_FAILED(create_res))
	{
		result.message = "xrCreateInstance failed with XrResult=" + std::to_string((int)create_res);
		return false;
	}

	result.instance_created = true;

	XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
	const XrResult props_res = xrGetInstanceProperties(instance_out, &instance_props);
	if(XR_SUCCEEDED(props_res))
	{
		result.runtime_name = instance_props.runtimeName;
		result.runtime_version_string = makeVersionString(instance_props.runtimeVersion);
	}

	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	const XrResult system_res = xrGetSystem(instance_out, &system_info, &system_id_out);
	if(XR_FAILED(system_res))
	{
		result.message = "OpenXR instance created, but xrGetSystem failed with XrResult=" + std::to_string((int)system_res);
		return false;
	}

	result.system_available = true;

	XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };
	const XrResult system_props_res = xrGetSystemProperties(instance_out, system_id_out, &system_props);
	if(XR_SUCCEEDED(system_props_res))
	{
		result.system_properties_queried = true;
		result.system_name = system_props.systemName;
	}

	result.message = "OpenXR runtime is reachable and reported an HMD system.";
	return true;
}


#if defined(_WIN32)
static std::string getCurrentOpenGLVersionString()
{
	const char* version_str = (const char*)glGetString(GL_VERSION);
	return version_str ? std::string(version_str) : std::string();
}


static bool getCurrentOpenGLVersion(uint64_t& version_out)
{
	GLint major = 0;
	GLint minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);

	if(major <= 0)
		return false;

	version_out = XR_MAKE_VERSION((uint32_t)major, (uint32_t)minor, 0);
	return true;
}
#endif
#endif
}


#if defined(XR_SUPPORT)
struct XRSession::OpaqueState
{
	struct ViewSwapchain
	{
		ViewSwapchain()
		:	swapchain(XR_NULL_HANDLE),
			format(0),
			width(0),
			height(0)
		{}

		XrSwapchain swapchain;
		int64_t format;
		uint32_t width;
		uint32_t height;
		RenderBufferRef depth_renderbuffer;
		std::vector<XrSwapchainImageOpenGLKHR> images;
		std::vector<FrameBufferRef> framebuffers;
	};

	struct HandActionState
	{
		HandActionState()
		:	user_path(XR_NULL_PATH),
			grip_space(XR_NULL_HANDLE),
			aim_space(XR_NULL_HANDLE)
		{}

		XrPath user_path;
		XrSpace grip_space;
		XrSpace aim_space;
	};

	OpaqueState()
	:	instance(XR_NULL_HANDLE),
		system_id(XR_NULL_SYSTEM_ID),
		session(XR_NULL_HANDLE),
		app_space(XR_NULL_HANDLE),
		view_space(XR_NULL_HANDLE),
		app_space_type(XR_REFERENCE_SPACE_TYPE_LOCAL),
		session_state(XR_SESSION_STATE_UNKNOWN),
		session_running(false),
		environment_blend_mode(XR_ENVIRONMENT_BLEND_MODE_OPAQUE),
		swapchain_format(0),
		tracking_space_calibrated(false),
		tracking_space_calibration_pending(true),
		tracking_base_yaw_offset(0.f),
		tracking_base_orientation_offset(Quatf::identity()),
		calibration_world_heading(0.f),
		calibration_tracking_head_pos_engine(0.f),
		calibration_candidate_start_time(-1.0),
		calibration_candidate_pose_valid(false),
		action_set(XR_NULL_HANDLE),
		grip_pose_action(XR_NULL_HANDLE),
		aim_pose_action(XR_NULL_HANDLE),
		select_action(XR_NULL_HANDLE),
		trigger_action(XR_NULL_HANDLE),
		move2d_action(XR_NULL_HANDLE)
	{
		std::memset(&view_state, 0, sizeof(view_state));
		view_state.type = XR_TYPE_VIEW_STATE;
	}

	XrInstance instance;
	XrSystemId system_id;
	XrSession session;
	XrSpace app_space;
	XrSpace view_space;
	XrReferenceSpaceType app_space_type;
	XrSessionState session_state;
	bool session_running;
	XrEnvironmentBlendMode environment_blend_mode;
	std::vector<XrViewConfigurationView> view_config_views;
	std::vector<XrEnvironmentBlendMode> environment_blend_modes;
	std::vector<ViewSwapchain> view_swapchains;
	std::vector<XrView> views;
	XrViewState view_state;
	int64_t swapchain_format;
	bool tracking_space_calibrated;
	bool tracking_space_calibration_pending;
	float tracking_base_yaw_offset;
	Quatf tracking_base_orientation_offset;
	float calibration_world_heading;
	Vec3f calibration_tracking_head_pos_engine;
	double calibration_candidate_start_time;
	XrPosef calibration_candidate_pose;
	bool calibration_candidate_pose_valid;
	XrActionSet action_set;
	XrAction grip_pose_action;
	XrAction aim_pose_action;
	XrAction select_action;
	XrAction trigger_action;
	XrAction move2d_action;
	HandActionState hands[2];
};


template <class State>
static void resetTrackingCalibrationState(State& state, bool pending_recalibration)
{
	state.tracking_space_calibrated = false;
	state.tracking_space_calibration_pending = pending_recalibration;
	state.tracking_base_yaw_offset = 0.f;
	state.tracking_base_orientation_offset = Quatf::identity();
	state.calibration_world_heading = 0.f;
	state.calibration_tracking_head_pos_engine = Vec3f(0.f);
	state.calibration_candidate_start_time = -1.0;
	state.calibration_candidate_pose_valid = false;
	std::memset(&state.calibration_candidate_pose, 0, sizeof(state.calibration_candidate_pose));
	state.calibration_candidate_pose.orientation.w = 1.f;
}


template <class State>
static void destroyViewSwapchains(State& state)
{
	state.view_swapchains.clear();
	state.swapchain_format = 0;
	resetTrackingCalibrationState(state, /*pending_recalibration=*/false);
}


struct ActionBindingDef
{
	XrAction action;
	const char* path_string;
};


template <class State>
static void destroyActionSubsystem(XRHandInputState& left_hand_state_out, XRHandInputState& right_hand_state_out, State& state)
{
	for(size_t i=0; i<2; ++i)
	{
		if(state.hands[i].grip_space != XR_NULL_HANDLE)
		{
			xrDestroySpace(state.hands[i].grip_space);
			state.hands[i].grip_space = XR_NULL_HANDLE;
		}

		if(state.hands[i].aim_space != XR_NULL_HANDLE)
		{
			xrDestroySpace(state.hands[i].aim_space);
			state.hands[i].aim_space = XR_NULL_HANDLE;
		}

		state.hands[i].user_path = XR_NULL_PATH;
	}

	if(state.trigger_action != XR_NULL_HANDLE)
	{
		xrDestroyAction(state.trigger_action);
		state.trigger_action = XR_NULL_HANDLE;
	}

	if(state.move2d_action != XR_NULL_HANDLE)
	{
		xrDestroyAction(state.move2d_action);
		state.move2d_action = XR_NULL_HANDLE;
	}

	if(state.select_action != XR_NULL_HANDLE)
	{
		xrDestroyAction(state.select_action);
		state.select_action = XR_NULL_HANDLE;
	}

	if(state.aim_pose_action != XR_NULL_HANDLE)
	{
		xrDestroyAction(state.aim_pose_action);
		state.aim_pose_action = XR_NULL_HANDLE;
	}

	if(state.grip_pose_action != XR_NULL_HANDLE)
	{
		xrDestroyAction(state.grip_pose_action);
		state.grip_pose_action = XR_NULL_HANDLE;
	}

	if(state.action_set != XR_NULL_HANDLE)
	{
		xrDestroyActionSet(state.action_set);
		state.action_set = XR_NULL_HANDLE;
	}

	resetHandInputState(left_hand_state_out);
	resetHandInputState(right_hand_state_out);
}


static bool createAction(XrActionSet action_set, const char* action_name, const char* localised_name, XrActionType action_type, const XrPath* subaction_paths,
	uint32_t subaction_path_count, XrAction& action_out, std::string& error_out)
{
	XrActionCreateInfo create_info = { XR_TYPE_ACTION_CREATE_INFO };
	create_info.actionType = action_type;
	copyCStringTruncated(create_info.actionName, XR_MAX_ACTION_NAME_SIZE, action_name);
	copyCStringTruncated(create_info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, localised_name);
	create_info.countSubactionPaths = subaction_path_count;
	create_info.subactionPaths = subaction_paths;

	const XrResult create_res = xrCreateAction(action_set, &create_info, &action_out);
	if(XR_FAILED(create_res))
	{
		error_out = "xrCreateAction('" + std::string(action_name) + "') failed with XrResult=" + std::to_string((int)create_res);
		return false;
	}

	return true;
}


static bool createActionSpace(XrSession session, XrAction action, XrPath subaction_path, XrSpace& space_out, std::string& error_out)
{
	XrActionSpaceCreateInfo create_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
	create_info.action = action;
	create_info.subactionPath = subaction_path;
	create_info.poseInActionSpace.orientation.w = 1.f;

	const XrResult create_res = xrCreateActionSpace(session, &create_info, &space_out);
	if(XR_FAILED(create_res))
	{
		error_out = "xrCreateActionSpace failed with XrResult=" + std::to_string((int)create_res);
		return false;
	}

	return true;
}


static bool suggestBindingsForProfile(XrInstance instance, const char* interaction_profile_path, const ActionBindingDef* defs, size_t def_count, std::string& error_out)
{
	XrPath interaction_profile = XR_NULL_PATH;
	if(!xrStringToPathChecked(instance, interaction_profile_path, interaction_profile, error_out))
		return false;

	std::vector<XrActionSuggestedBinding> bindings(def_count);
	for(size_t i=0; i<def_count; ++i)
	{
		XrPath binding_path = XR_NULL_PATH;
		if(!xrStringToPathChecked(instance, defs[i].path_string, binding_path, error_out))
			return false;

		bindings[i].action = defs[i].action;
		bindings[i].binding = binding_path;
	}

	XrInteractionProfileSuggestedBinding suggested_bindings = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested_bindings.interactionProfile = interaction_profile;
	suggested_bindings.countSuggestedBindings = (uint32_t)bindings.size();
	suggested_bindings.suggestedBindings = bindings.empty() ? NULL : &bindings[0];

	const XrResult suggest_res = xrSuggestInteractionProfileBindings(instance, &suggested_bindings);
	if(XR_FAILED(suggest_res))
	{
		error_out = "xrSuggestInteractionProfileBindings('" + std::string(interaction_profile_path) + "') failed with XrResult=" + std::to_string((int)suggest_res);
		return false;
	}

	return true;
}


template <class State>
static bool initialiseActionSubsystem(XRRuntimeProbeResult& result, XRHandInputState& left_hand_state_out, XRHandInputState& right_hand_state_out, State& state)
{
	result.action_set_created = false;
	result.pose_actions_created = false;
	result.input_actions_created = false;
	result.action_spaces_created = false;
	result.action_bindings_suggested = false;
	result.action_sets_attached = false;
	result.actions_synced = false;
	result.suggested_binding_profile_count = 0;
	result.actions_message.clear();

	resetHandInputState(left_hand_state_out);
	resetHandInputState(right_hand_state_out);

	std::string error;
	if(!xrStringToPathChecked(state.instance, "/user/hand/left", state.hands[0].user_path, error))
	{
		result.actions_message = error;
		return false;
	}

	if(!xrStringToPathChecked(state.instance, "/user/hand/right", state.hands[1].user_path, error))
	{
		result.actions_message = error;
		return false;
	}

	left_hand_state_out.subaction_path_valid = true;
	right_hand_state_out.subaction_path_valid = true;

	const XrPath hand_subaction_paths[2] = { state.hands[0].user_path, state.hands[1].user_path };

	XrActionSetCreateInfo action_set_create_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	copyCStringTruncated(action_set_create_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "gameplay");
	copyCStringTruncated(action_set_create_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Metasiberia Gameplay");
	action_set_create_info.priority = 0;

	const XrResult action_set_res = xrCreateActionSet(state.instance, &action_set_create_info, &state.action_set);
	if(XR_FAILED(action_set_res))
	{
		result.actions_message = "xrCreateActionSet failed with XrResult=" + std::to_string((int)action_set_res);
		return false;
	}

	result.action_set_created = true;

	if(!createAction(state.action_set, "hand_grip_pose", "Hand Grip Pose", XR_ACTION_TYPE_POSE_INPUT, hand_subaction_paths, 2, state.grip_pose_action, error))
	{
		result.actions_message = error;
		return false;
	}

	if(!createAction(state.action_set, "hand_aim_pose", "Hand Aim Pose", XR_ACTION_TYPE_POSE_INPUT, hand_subaction_paths, 2, state.aim_pose_action, error))
	{
		result.actions_message = error;
		return false;
	}

	result.pose_actions_created = true;

	if(!createAction(state.action_set, "hand_select", "Hand Select", XR_ACTION_TYPE_BOOLEAN_INPUT, hand_subaction_paths, 2, state.select_action, error))
	{
		result.actions_message = error;
		return false;
	}

	if(!createAction(state.action_set, "hand_trigger_value", "Hand Trigger Value", XR_ACTION_TYPE_FLOAT_INPUT, hand_subaction_paths, 2, state.trigger_action, error))
	{
		result.actions_message = error;
		return false;
	}

	if(!createAction(state.action_set, "hand_move2d", "Hand Move2D", XR_ACTION_TYPE_VECTOR2F_INPUT, hand_subaction_paths, 2, state.move2d_action, error))
	{
		result.actions_message = error;
		return false;
	}

	result.input_actions_created = true;

	uint32_t successful_binding_profile_count = 0;
	std::string last_binding_error;

	{
		const ActionBindingDef bindings[] = {
			{ state.select_action, "/user/hand/left/input/select/click" },
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.select_action, "/user/hand/right/input/select/click" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/khr/simple_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.move2d_action, "/user/hand/left/input/thumbstick" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.move2d_action, "/user/hand/right/input/thumbstick" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/oculus/touch_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.move2d_action, "/user/hand/left/input/thumbstick" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.move2d_action, "/user/hand/right/input/thumbstick" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/valve/index_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.select_action, "/user/hand/left/input/trackpad/click" },
			{ state.move2d_action, "/user/hand/left/input/trackpad" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.select_action, "/user/hand/right/input/trackpad/click" },
			{ state.move2d_action, "/user/hand/right/input/trackpad" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/htc/vive_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.move2d_action, "/user/hand/left/input/thumbstick" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.move2d_action, "/user/hand/right/input/thumbstick" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/microsoft/motion_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.move2d_action, "/user/hand/left/input/thumbstick" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.move2d_action, "/user/hand/right/input/thumbstick" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/htc/vive_cosmos_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	{
		const ActionBindingDef bindings[] = {
			{ state.grip_pose_action, "/user/hand/left/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/left/input/aim/pose" },
			{ state.trigger_action, "/user/hand/left/input/trigger/value" },
			{ state.move2d_action, "/user/hand/left/input/thumbstick" },
			{ state.grip_pose_action, "/user/hand/right/input/grip/pose" },
			{ state.aim_pose_action, "/user/hand/right/input/aim/pose" },
			{ state.trigger_action, "/user/hand/right/input/trigger/value" },
			{ state.move2d_action, "/user/hand/right/input/thumbstick" }
		};

		if(suggestBindingsForProfile(state.instance, "/interaction_profiles/htc/vive_focus3_controller", bindings, sizeof(bindings) / sizeof(bindings[0]), error))
			successful_binding_profile_count++;
		else
			last_binding_error = error;
	}

	result.suggested_binding_profile_count = successful_binding_profile_count;
	result.action_bindings_suggested = (successful_binding_profile_count > 0);

	XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach_info.countActionSets = 1;
	attach_info.actionSets = &state.action_set;
	const XrResult attach_res = xrAttachSessionActionSets(state.session, &attach_info);
	if(XR_FAILED(attach_res))
	{
		result.actions_message = "xrAttachSessionActionSets failed with XrResult=" + std::to_string((int)attach_res);
		return false;
	}

	result.action_sets_attached = true;

	for(size_t i=0; i<2; ++i)
	{
		if(!createActionSpace(state.session, state.grip_pose_action, state.hands[i].user_path, state.hands[i].grip_space, error))
		{
			result.actions_message = error;
			return false;
		}

		if(!createActionSpace(state.session, state.aim_pose_action, state.hands[i].user_path, state.hands[i].aim_space, error))
		{
			result.actions_message = error;
			return false;
		}
	}

	result.action_spaces_created = true;

	if(result.action_bindings_suggested)
		result.actions_message = "OpenXR action subsystem initialised with " + std::to_string(result.suggested_binding_profile_count) + " suggested interaction profile binding set(s).";
	else if(!last_binding_error.empty())
		result.actions_message = "OpenXR action subsystem initialised, but no interaction profile bindings were accepted. Last binding error: " + last_binding_error;
	else
		result.actions_message = "OpenXR action subsystem initialised, but no interaction profile bindings were accepted.";

	return true;
}


static bool queryCurrentInteractionProfile(XrInstance instance, XrSession session, XrPath user_path, XRHandInputState& hand_state_out, std::string& error_out)
{
	XrInteractionProfileState profile_state = { XR_TYPE_INTERACTION_PROFILE_STATE };
	const XrResult profile_res = xrGetCurrentInteractionProfile(session, user_path, &profile_state);
	if(XR_FAILED(profile_res))
	{
		error_out = "xrGetCurrentInteractionProfile failed with XrResult=" + std::to_string((int)profile_res);
		return false;
	}

	hand_state_out.interaction_profile_valid = (profile_state.interactionProfile != XR_NULL_PATH);
	hand_state_out.interaction_profile = xrPathToStringSafe(instance, profile_state.interactionProfile);
	return true;
}


static bool queryBooleanActionState(XrSession session, XrAction action, XrPath subaction_path, bool& is_active_out, bool& current_state_out, std::string& error_out)
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	get_info.action = action;
	get_info.subactionPath = subaction_path;

	XrActionStateBoolean action_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
	const XrResult state_res = xrGetActionStateBoolean(session, &get_info, &action_state);
	if(XR_FAILED(state_res))
	{
		error_out = "xrGetActionStateBoolean failed with XrResult=" + std::to_string((int)state_res);
		return false;
	}

	is_active_out = (action_state.isActive == XR_TRUE);
	current_state_out = (action_state.currentState == XR_TRUE);
	return true;
}


static bool queryFloatActionState(XrSession session, XrAction action, XrPath subaction_path, bool& is_active_out, float& current_state_out, std::string& error_out)
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	get_info.action = action;
	get_info.subactionPath = subaction_path;

	XrActionStateFloat action_state = { XR_TYPE_ACTION_STATE_FLOAT };
	const XrResult state_res = xrGetActionStateFloat(session, &get_info, &action_state);
	if(XR_FAILED(state_res))
	{
		error_out = "xrGetActionStateFloat failed with XrResult=" + std::to_string((int)state_res);
		return false;
	}

	is_active_out = (action_state.isActive == XR_TRUE);
	current_state_out = action_state.currentState;
	return true;
}


static bool queryVector2fActionState(XrSession session, XrAction action, XrPath subaction_path, bool& is_active_out, Vec2f& current_state_out, std::string& error_out)
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	get_info.action = action;
	get_info.subactionPath = subaction_path;

	XrActionStateVector2f action_state = { XR_TYPE_ACTION_STATE_VECTOR2F };
	const XrResult state_res = xrGetActionStateVector2f(session, &get_info, &action_state);
	if(XR_FAILED(state_res))
	{
		error_out = "xrGetActionStateVector2f failed with XrResult=" + std::to_string((int)state_res);
		return false;
	}

	is_active_out = (action_state.isActive == XR_TRUE);
	current_state_out = Vec2f(action_state.currentState.x, action_state.currentState.y);
	return true;
}


static bool locateActionSpace(XrSpace space, XrSpace base_space, XrTime display_time, const Quatf& orientation_offset, float yaw_offset, const Vec3f& world_translation, XRTrackedPoseState& pose_state_out,
	std::string& error_out)
{
	resetTrackedPoseState(pose_state_out);

	if(space == XR_NULL_HANDLE)
		return true;

	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	const XrResult locate_res = xrLocateSpace(space, base_space, display_time, &location);
	if(XR_FAILED(locate_res))
	{
		error_out = "xrLocateSpace failed with XrResult=" + std::to_string((int)locate_res);
		return false;
	}

	const XrSpaceLocationFlags flags = location.locationFlags;
	pose_state_out.position_valid = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
	pose_state_out.orientation_valid = (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
	pose_state_out.position_tracked = (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
	pose_state_out.orientation_tracked = (flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
	pose_state_out.active = pose_state_out.position_valid || pose_state_out.orientation_valid;

	if(pose_state_out.position_valid && pose_state_out.orientation_valid)
		buildObjectToWorldMatrixFromXRPose(location.pose, orientation_offset, yaw_offset, world_translation, pose_state_out.object_to_world_matrix);

	return true;
}


template <class State>
static bool createViewSwapchains(XRRuntimeProbeResult& result, State& state)
{
#if !defined(_WIN32)
	(void)result;
	(void)state;
	return false;
#else
	uint32_t runtime_format_count = 0;
	const XrResult format_count_res = xrEnumerateSwapchainFormats(state.session, 0, &runtime_format_count, NULL);
	if(XR_FAILED(format_count_res) || runtime_format_count == 0)
	{
		result.message = "xrEnumerateSwapchainFormats(count) failed with XrResult=" + std::to_string((int)format_count_res);
		return false;
	}

	std::vector<int64_t> runtime_formats(runtime_format_count);
	const XrResult format_fill_res = xrEnumerateSwapchainFormats(state.session, runtime_format_count, &runtime_format_count, &runtime_formats[0]);
	if(XR_FAILED(format_fill_res))
	{
		result.message = "xrEnumerateSwapchainFormats(fill) failed with XrResult=" + std::to_string((int)format_fill_res);
		return false;
	}

	int64_t chosen_format = 0;
	if(!chooseSwapchainFormat(runtime_formats, chosen_format))
	{
		result.message = "No supported OpenGL swapchain format was found in the runtime format list.";
		return false;
	}

	const GLuint prev_draw_fbo = FrameBuffer::getCurrentlyBoundDrawFrameBuffer();
	state.view_swapchains.clear();
	state.view_swapchains.resize(state.view_config_views.size());
	state.swapchain_format = chosen_format;

	for(size_t i=0; i<state.view_config_views.size(); ++i)
	{
		auto& view_swapchain = state.view_swapchains[i];
		view_swapchain.format = chosen_format;
		view_swapchain.width = std::max(1u, state.view_config_views[i].recommendedImageRectWidth);
		view_swapchain.height = std::max(1u, state.view_config_views[i].recommendedImageRectHeight);
		clampRecommendedSwapchainSizeForRuntime(result, view_swapchain.width, view_swapchain.height);

		XrSwapchainCreateInfo swapchain_create_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchain_create_info.createFlags = 0;
		swapchain_create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		swapchain_create_info.format = chosen_format;
		swapchain_create_info.sampleCount = 1;
		swapchain_create_info.width = view_swapchain.width;
		swapchain_create_info.height = view_swapchain.height;
		swapchain_create_info.faceCount = 1;
		swapchain_create_info.arraySize = 1;
		swapchain_create_info.mipCount = 1;

		const XrResult create_swapchain_res = xrCreateSwapchain(state.session, &swapchain_create_info, &view_swapchain.swapchain);
		if(XR_FAILED(create_swapchain_res))
		{
			result.message = "xrCreateSwapchain failed for view " + std::to_string(i) + " with XrResult=" + std::to_string((int)create_swapchain_res);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
			return false;
		}

		uint32_t image_count = 0;
		const XrResult image_count_res = xrEnumerateSwapchainImages(view_swapchain.swapchain, 0, &image_count, NULL);
		if(XR_FAILED(image_count_res) || image_count == 0)
		{
			result.message = "xrEnumerateSwapchainImages(count) failed for view " + std::to_string(i) + " with XrResult=" + std::to_string((int)image_count_res);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
			return false;
		}

		view_swapchain.images.resize(image_count);
		for(size_t z=0; z<view_swapchain.images.size(); ++z)
			view_swapchain.images[z].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;

		const XrResult image_fill_res = xrEnumerateSwapchainImages(
			view_swapchain.swapchain,
			image_count,
			&image_count,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(&view_swapchain.images[0])
		);
		if(XR_FAILED(image_fill_res))
		{
			result.message = "xrEnumerateSwapchainImages(fill) failed for view " + std::to_string(i) + " with XrResult=" + std::to_string((int)image_fill_res);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
			return false;
		}

		view_swapchain.depth_renderbuffer = new RenderBuffer(view_swapchain.width, view_swapchain.height, /*MSAA_samples=*/1, OpenGLTextureFormat::Format_Depth_Float);
		view_swapchain.framebuffers.resize(image_count);

		for(size_t z=0; z<view_swapchain.images.size(); ++z)
		{
			FrameBufferRef framebuffer = new FrameBuffer();
			framebuffer->attachTextureName(GL_TEXTURE_2D, view_swapchain.images[z].image, view_swapchain.width, view_swapchain.height, GL_COLOR_ATTACHMENT0);
			framebuffer->attachRenderBuffer(*view_swapchain.depth_renderbuffer, GL_DEPTH_ATTACHMENT);
			if(!framebuffer->isComplete())
			{
				result.message = "OpenXR swapchain framebuffer was incomplete for view " + std::to_string(i) + ", image " + std::to_string(z) + ".";
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
				return false;
			}

			view_swapchain.framebuffers[z] = framebuffer;
		}
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);

	result.swapchains_created = true;
	result.swapchain_format_string = swapchainFormatString(chosen_format) + " (runtime formats: " + swapchainFormatListString(runtime_formats) + ")";
	return true;
#endif
}
#endif


XRSession::XRSession()
:	last_result(makeDefaultResult())
#if defined(XR_SUPPORT)
	, state(new OpaqueState())
#endif
{}


XRSession::~XRSession()
{
	shutdown();

#if defined(XR_SUPPORT)
	delete state;
	state = NULL;
#endif
}


void XRSession::requestRecenter()
{
#if defined(XR_SUPPORT)
	if(!state)
		return;

	resetTrackingCalibrationState(*state, /*pending_recalibration=*/true);
	last_result.message = "Manual XR recenter requested; waiting for a stable focused head pose.";
#endif
}


XRRuntimeProbeResult XRSession::probeRuntime()
{
	XRRuntimeProbeResult result = makeDefaultResult();

#if defined(XR_SUPPORT)
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system_id = XR_NULL_SYSTEM_ID;
	initialiseInstanceAndSystem(result, /*enable_opengl_extension=*/false, instance, system_id);
	if(instance != XR_NULL_HANDLE)
		xrDestroyInstance(instance);
#else
	(void)result;
#endif

	return result;
}


bool XRSession::initialiseForCurrentOpenGLContext()
{
	shutdown();
	last_result = makeDefaultResult();

#if !defined(XR_SUPPORT)
	return false;
#elif !defined(_WIN32)
	last_result.xr_compiled_in = true;
	last_result.backend_name = "OpenXR";
	last_result.message = "OpenXR session bootstrap is currently implemented only for the Win32 Qt/OpenGL client.";
	return false;
#else
	if(!state)
	{
		last_result.xr_compiled_in = true;
		last_result.backend_name = "OpenXR";
		last_result.message = "XRSession internal state was not allocated.";
		return false;
	}

	const HGLRC current_glrc = wglGetCurrentContext();
	const HDC current_hdc = wglGetCurrentDC();
	if(!current_glrc || !current_hdc)
	{
		last_result.xr_compiled_in = true;
		last_result.backend_name = "OpenXR";
		last_result.message = "No current Win32 OpenGL context was bound while attempting OpenXR session bootstrap.";
		return false;
	}

	if(!initialiseInstanceAndSystem(last_result, /*enable_opengl_extension=*/true, state->instance, state->system_id))
	{
		shutdown();
		return false;
	}

	PFN_xrGetOpenGLGraphicsRequirementsKHR get_graphics_requirements = NULL;
	const XrResult proc_res = xrGetInstanceProcAddr(
		state->instance,
		"xrGetOpenGLGraphicsRequirementsKHR",
		(PFN_xrVoidFunction*)&get_graphics_requirements
	);
	if(XR_FAILED(proc_res) || !get_graphics_requirements)
	{
		last_result.message = "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR) failed with XrResult=" + std::to_string((int)proc_res);
		shutdown();
		return false;
	}

	XrGraphicsRequirementsOpenGLKHR graphics_requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	const XrResult requirements_res = get_graphics_requirements(state->instance, state->system_id, &graphics_requirements);
	if(XR_FAILED(requirements_res))
	{
		last_result.message = "xrGetOpenGLGraphicsRequirementsKHR failed with XrResult=" + std::to_string((int)requirements_res);
		shutdown();
		return false;
	}

	last_result.graphics_requirements_obtained = true;
	last_result.graphics_api_min_version_string = makeVersionString(graphics_requirements.minApiVersionSupported);
	last_result.graphics_api_max_version_string = makeVersionString(graphics_requirements.maxApiVersionSupported);
	last_result.graphics_api_version_string = getCurrentOpenGLVersionString();

	uint64_t current_gl_version = 0;
	if(getCurrentOpenGLVersion(current_gl_version))
	{
		if(current_gl_version < graphics_requirements.minApiVersionSupported || current_gl_version > graphics_requirements.maxApiVersionSupported)
		{
			last_result.message =
				"Current OpenGL context version " + last_result.graphics_api_version_string +
				" is outside OpenXR runtime requirements [" + last_result.graphics_api_min_version_string +
				", " + last_result.graphics_api_max_version_string + "].";
			shutdown();
			return false;
		}
	}

	XrGraphicsBindingOpenGLWin32KHR graphics_binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	graphics_binding.hDC = current_hdc;
	graphics_binding.hGLRC = current_glrc;

	XrSessionCreateInfo session_create_info = { XR_TYPE_SESSION_CREATE_INFO };
	session_create_info.next = &graphics_binding;
	session_create_info.systemId = state->system_id;

	const XrResult session_res = xrCreateSession(state->instance, &session_create_info, &state->session);
	if(XR_FAILED(session_res))
	{
		last_result.message = "xrCreateSession failed with XrResult=" + std::to_string((int)session_res);
		shutdown();
		return false;
	}

	last_result.session_created = true;
	last_result.graphics_requirements_compatible = true;

	XrReferenceSpaceType chosen_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	uint32_t reference_space_count = 0;
	const XrResult reference_space_count_res = xrEnumerateReferenceSpaces(state->session, 0, &reference_space_count, NULL);
	if(XR_SUCCEEDED(reference_space_count_res) && reference_space_count > 0)
	{
		std::vector<XrReferenceSpaceType> available_space_types(reference_space_count);
		const XrResult reference_space_fill_res = xrEnumerateReferenceSpaces(state->session, reference_space_count, &reference_space_count, &available_space_types[0]);
		if(XR_SUCCEEDED(reference_space_fill_res))
			chooseReferenceSpaceType(available_space_types, chosen_space_type);
	}

	state->app_space_type = chosen_space_type;
	XrReferenceSpaceCreateInfo space_create_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	space_create_info.referenceSpaceType = state->app_space_type;
	space_create_info.poseInReferenceSpace.orientation.w = 1.f;

	XrResult space_res = xrCreateReferenceSpace(state->session, &space_create_info, &state->app_space);
	if(XR_FAILED(space_res) && (state->app_space_type != XR_REFERENCE_SPACE_TYPE_LOCAL))
	{
		state->app_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
		space_create_info.referenceSpaceType = state->app_space_type;
		space_res = xrCreateReferenceSpace(state->session, &space_create_info, &state->app_space);
	}

	if(XR_FAILED(space_res))
	{
		last_result.message = "xrCreateReferenceSpace(" + referenceSpaceTypeString(space_create_info.referenceSpaceType) + ") failed with XrResult=" + std::to_string((int)space_res);
		shutdown();
		return false;
	}

	last_result.local_reference_space_created = true;
	last_result.reference_space_type_string = referenceSpaceTypeString(state->app_space_type);

	XrReferenceSpaceCreateInfo view_space_create_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	view_space_create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	view_space_create_info.poseInReferenceSpace.orientation.w = 1.f;
	const XrResult view_space_res = xrCreateReferenceSpace(state->session, &view_space_create_info, &state->view_space);
	if(XR_FAILED(view_space_res))
		state->view_space = XR_NULL_HANDLE;

	if(!initialiseActionSubsystem(last_result, left_hand_state, right_hand_state, *state) && last_result.actions_message.empty())
		last_result.actions_message = "OpenXR action subsystem initialisation did not complete.";

	uint32_t view_count = 0;
	const XrResult view_count_res = xrEnumerateViewConfigurationViews(
		state->instance,
		state->system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		0,
		&view_count,
		NULL
	);
	if(XR_FAILED(view_count_res) || view_count == 0)
	{
		last_result.message = "xrEnumerateViewConfigurationViews(count) failed with XrResult=" + std::to_string((int)view_count_res);
		shutdown();
		return false;
	}

	state->view_config_views.resize(view_count);
	for(size_t i=0; i<state->view_config_views.size(); ++i)
		state->view_config_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;

	const XrResult view_fill_res = xrEnumerateViewConfigurationViews(
		state->instance,
		state->system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		view_count,
		&view_count,
		&state->view_config_views[0]
	);
	if(XR_FAILED(view_fill_res))
	{
		last_result.message = "xrEnumerateViewConfigurationViews(fill) failed with XrResult=" + std::to_string((int)view_fill_res);
		shutdown();
		return false;
	}

	last_result.view_configuration_enumerated = true;
	last_result.configured_view_count = view_count;

	state->views.resize(view_count);
	for(size_t i=0; i<state->views.size(); ++i)
		state->views[i].type = XR_TYPE_VIEW;

	uint32_t blend_mode_count = 0;
	const XrResult blend_count_res = xrEnumerateEnvironmentBlendModes(
		state->instance,
		state->system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		0,
		&blend_mode_count,
		NULL
	);
	if(XR_FAILED(blend_count_res) || blend_mode_count == 0)
	{
		last_result.message = "xrEnumerateEnvironmentBlendModes(count) failed with XrResult=" + std::to_string((int)blend_count_res);
		shutdown();
		return false;
	}

	state->environment_blend_modes.resize(blend_mode_count);
	const XrResult blend_fill_res = xrEnumerateEnvironmentBlendModes(
		state->instance,
		state->system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		blend_mode_count,
		&blend_mode_count,
		&state->environment_blend_modes[0]
	);
	if(XR_FAILED(blend_fill_res))
	{
		last_result.message = "xrEnumerateEnvironmentBlendModes(fill) failed with XrResult=" + std::to_string((int)blend_fill_res);
		shutdown();
		return false;
	}

	if(!chooseEnvironmentBlendMode(state->environment_blend_modes, state->environment_blend_mode))
	{
		last_result.message = "Could not choose a supported environment blend mode for XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO.";
		shutdown();
		return false;
	}

	if(!createViewSwapchains(last_result, *state))
	{
		shutdown();
		return false;
	}

	last_result.environment_blend_mode_selected = true;
	last_result.environment_blend_mode_string = environmentBlendModeString(state->environment_blend_mode);
	last_result.session_state_string = sessionStateString(state->session_state);
	last_result.message = "OpenXR session bootstrap and stereo swapchain allocation succeeded for the current Win32 Qt/OpenGL context.";
	return true;
#endif
}


void XRSession::renderFrame(OpenGLEngine& opengl_engine, const CameraController& cam_controller, float near_draw_dist, float max_draw_dist)
{
#if defined(XR_SUPPORT)
	if(!state || !isInitialised())
		return;

	last_mirror_view.valid = false;
	resetTrackedPoseState(head_pose_state);
	resetTrackedPoseState(raw_head_pose_state);
	resetEyeViewState(left_eye_view_state);
	resetEyeViewState(right_eye_view_state);
	last_result.frame_loop_active = false;
	last_result.views_located = false;
	last_result.should_render_current_frame = false;
	last_result.projection_layer_submitted = false;
	last_result.located_view_count = 0;
	last_result.actions_synced = false;

	resetHandInputState(left_hand_state);
	resetHandInputState(right_hand_state);
	left_hand_state.subaction_path_valid = (state->hands[0].user_path != XR_NULL_PATH);
	right_hand_state.subaction_path_valid = (state->hands[1].user_path != XR_NULL_PATH);

	XrEventDataBuffer event;
	resetEventDataBuffer(event);

	for(;;)
	{
		const XrResult poll_res = xrPollEvent(state->instance, &event);
		if(poll_res == XR_EVENT_UNAVAILABLE)
			break;

		if(XR_FAILED(poll_res))
		{
			last_result.message = "xrPollEvent failed with XrResult=" + std::to_string((int)poll_res);
			return;
		}

		if(event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			const XrEventDataSessionStateChanged* session_state_changed = (const XrEventDataSessionStateChanged*)&event;
			state->session_state = session_state_changed->state;
			last_result.session_state_string = sessionStateString(state->session_state);

			if(state->session_state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
				begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

				const XrResult begin_session_res = xrBeginSession(state->session, &begin_info);
				if(XR_FAILED(begin_session_res))
				{
					last_result.message = "xrBeginSession failed with XrResult=" + std::to_string((int)begin_session_res);
					return;
				}

				state->session_running = true;
				resetTrackingCalibrationState(*state, /*pending_recalibration=*/true);
				last_result.session_running = true;
				last_result.message = "OpenXR runtime entered READY and xrBeginSession succeeded.";
			}
			else if(state->session_state == XR_SESSION_STATE_STOPPING)
			{
				if(state->session_running)
				{
					const XrResult end_session_res = xrEndSession(state->session);
					if(XR_FAILED(end_session_res))
					{
						last_result.message = "xrEndSession failed with XrResult=" + std::to_string((int)end_session_res);
						return;
					}
				}

				state->session_running = false;
				resetTrackingCalibrationState(*state, /*pending_recalibration=*/false);
				last_result.session_running = false;
				last_result.message = "OpenXR runtime requested session stop.";
			}
			else if(state->session_state == XR_SESSION_STATE_EXITING)
			{
				state->session_running = false;
				resetTrackingCalibrationState(*state, /*pending_recalibration=*/false);
				last_result.session_running = false;
				last_result.message = "OpenXR runtime requested application exit.";
			}
			else if(state->session_state == XR_SESSION_STATE_LOSS_PENDING)
			{
				state->session_running = false;
				resetTrackingCalibrationState(*state, /*pending_recalibration=*/false);
				last_result.session_running = false;
				last_result.message = "OpenXR runtime reported session loss pending.";
			}
			else
			{
				if(state->session_state == XR_SESSION_STATE_FOCUSED)
				{
					resetTrackingCalibrationState(*state, /*pending_recalibration=*/true);
					last_result.message = "OpenXR runtime entered FOCUSED; waiting for a stable worn-headset pose before refreshing head alignment.";
				}
				last_result.session_running = state->session_running;
			}
		}
		else if(event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			state->session_running = false;
			resetTrackingCalibrationState(*state, /*pending_recalibration=*/false);
			last_result.session_running = false;
			last_result.message = "OpenXR runtime reported instance loss pending.";
		}
		else if(event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
		{
			const XrEventDataReferenceSpaceChangePending* space_change = (const XrEventDataReferenceSpaceChangePending*)&event;
			if((space_change->session == state->session) && (space_change->referenceSpaceType == state->app_space_type))
			{
				resetTrackingCalibrationState(*state, /*pending_recalibration=*/true);
				last_result.message =
					"OpenXR " + referenceSpaceTypeString(state->app_space_type) +
					" reference space origin changed; waiting for a stable focused head pose before refreshing head alignment.";
			}
		}

		resetEventDataBuffer(event);
	}

	if(!sessionStateAllowsFrameLoop(state->session_state, state->session_running))
		return;

	XrFrameWaitInfo frame_wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	const XrResult wait_frame_res = xrWaitFrame(state->session, &frame_wait_info, &frame_state);
	if(XR_FAILED(wait_frame_res))
	{
		last_result.message = "xrWaitFrame failed with XrResult=" + std::to_string((int)wait_frame_res);
		return;
	}

	XrFrameBeginInfo frame_begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	const XrResult begin_frame_res = xrBeginFrame(state->session, &frame_begin_info);
	if(XR_FAILED(begin_frame_res))
	{
		last_result.message = "xrBeginFrame failed with XrResult=" + std::to_string((int)begin_frame_res);
		return;
	}

	last_result.frame_loop_active = true;
	last_result.should_render_current_frame = (frame_state.shouldRender == XR_TRUE);

	if(last_result.action_sets_attached && state->action_set != XR_NULL_HANDLE)
	{
		last_result.actions_message.clear();

		XrActiveActionSet active_action_set;
		std::memset(&active_action_set, 0, sizeof(active_action_set));
		active_action_set.actionSet = state->action_set;

		XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
		sync_info.countActiveActionSets = 1;
		sync_info.activeActionSets = &active_action_set;

		const XrResult sync_res = xrSyncActions(state->session, &sync_info);
		if(XR_FAILED(sync_res))
		{
			last_result.actions_message = "xrSyncActions failed with XrResult=" + std::to_string((int)sync_res);
		}
		else
		{
			last_result.actions_synced = true;
			std::string action_error;

			if(!queryCurrentInteractionProfile(state->instance, state->session, state->hands[0].user_path, left_hand_state, action_error))
				last_result.actions_message = action_error;

			if(!queryCurrentInteractionProfile(state->instance, state->session, state->hands[1].user_path, right_hand_state, action_error))
				last_result.actions_message = action_error;

			if(!queryBooleanActionState(state->session, state->select_action, state->hands[0].user_path, left_hand_state.select_active, left_hand_state.select_pressed, action_error))
				last_result.actions_message = action_error;

			if(!queryBooleanActionState(state->session, state->select_action, state->hands[1].user_path, right_hand_state.select_active, right_hand_state.select_pressed, action_error))
				last_result.actions_message = action_error;

			if(!queryFloatActionState(state->session, state->trigger_action, state->hands[0].user_path, left_hand_state.trigger_active, left_hand_state.trigger_value, action_error))
				last_result.actions_message = action_error;

			if(!queryFloatActionState(state->session, state->trigger_action, state->hands[1].user_path, right_hand_state.trigger_active, right_hand_state.trigger_value, action_error))
				last_result.actions_message = action_error;

			if(!queryVector2fActionState(state->session, state->move2d_action, state->hands[0].user_path, left_hand_state.move2d_active, left_hand_state.move2d_value, action_error))
				last_result.actions_message = action_error;

			if(!queryVector2fActionState(state->session, state->move2d_action, state->hands[1].user_path, right_hand_state.move2d_active, right_hand_state.move2d_value, action_error))
				last_result.actions_message = action_error;

			if(last_result.actions_message.empty())
				last_result.actions_message = "OpenXR actions synced for the current frame.";
		}
	}

	XrCompositionLayerProjection projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	std::vector<XrCompositionLayerProjectionView> projection_views;
	const XrCompositionLayerBaseHeader* submitted_layers[1] = { NULL };
	uint32_t submitted_layer_count = 0;

	if(last_result.should_render_current_frame && !state->views.empty() && !state->view_swapchains.empty())
	{
		XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
		locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locate_info.displayTime = frame_state.predictedDisplayTime;
		locate_info.space = state->app_space;

		std::memset(&state->view_state, 0, sizeof(XrViewState));
		state->view_state.type = XR_TYPE_VIEW_STATE;

		for(size_t i=0; i<state->views.size(); ++i)
		{
			std::memset(&state->views[i], 0, sizeof(XrView));
			state->views[i].type = XR_TYPE_VIEW;
		}

		uint32_t located_view_count = 0;
		const XrResult locate_views_res = xrLocateViews(
			state->session,
			&locate_info,
			&state->view_state,
			(uint32_t)state->views.size(),
			&located_view_count,
			&state->views[0]
		);
		if(XR_SUCCEEDED(locate_views_res))
		{
			last_result.views_located = true;
			last_result.located_view_count = std::min<uint32_t>(located_view_count, (uint32_t)state->view_swapchains.size());

			const bool position_valid = (state->view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
			const bool orientation_valid = (state->view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
			const bool position_tracked = (state->view_state.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
			const bool orientation_tracked = (state->view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;

			if(position_valid && orientation_valid && last_result.located_view_count > 0)
			{
				XrPosef head_pose = {};
				head_pose.orientation.w = 1.f;
				bool head_position_valid = position_valid;
				bool head_orientation_valid = orientation_valid;
				bool head_position_tracked = position_tracked;
				bool head_orientation_tracked = orientation_tracked;
				bool head_pose_ready = false;

				if(state->view_space != XR_NULL_HANDLE)
				{
					XrSpaceLocation head_location = { XR_TYPE_SPACE_LOCATION };
					const XrResult head_locate_res = xrLocateSpace(state->view_space, state->app_space, frame_state.predictedDisplayTime, &head_location);
					if(XR_SUCCEEDED(head_locate_res))
					{
						const XrSpaceLocationFlags head_flags = head_location.locationFlags;
						head_position_valid = (head_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
						head_orientation_valid = (head_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
						head_position_tracked = (head_flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
						head_orientation_tracked = (head_flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;

						if(head_position_valid && head_orientation_valid)
						{
							head_pose = head_location.pose;
							head_pose_ready = true;
						}
					}
				}

				if(!head_pose_ready && buildCalibrationHeadPose(state->views, last_result.located_view_count, head_pose))
					head_pose_ready = true;

				if(head_pose_ready)
				{
					const float current_world_heading = (float)cam_controller.getAngles().x;
					bool calibration_ready = !state->tracking_space_calibration_pending;
					if(state->tracking_space_calibration_pending && (state->session_state == XR_SESSION_STATE_FOCUSED))
					{
						const double now = Clock::getTimeSinceInit();

						// If SteamVR gives us a floor-relative STAGE space, don't calibrate while the headset is still resting low on a desk.
						if((state->app_space_type == XR_REFERENCE_SPACE_TYPE_STAGE) && !(head_pose.position.y >= 1.05f))
						{
							state->calibration_candidate_pose_valid = false;
							state->calibration_candidate_start_time = -1.0;
							last_result.message = "OpenXR head alignment is waiting for the headset to be worn at head height.";
						}
						else
						{
							const bool moved_since_candidate =
								!state->calibration_candidate_pose_valid ||
								(positionDistance(head_pose.position, state->calibration_candidate_pose.position) > 0.05f) ||
								(orientationAngleDelta(head_pose.orientation, state->calibration_candidate_pose.orientation) > degreeToRad(6.f));

							if(moved_since_candidate)
							{
								state->calibration_candidate_pose = head_pose;
								state->calibration_candidate_pose_valid = true;
								state->calibration_candidate_start_time = now;
								last_result.message = "OpenXR head alignment is waiting for a stable neutral headset pose.";
							}
							else if((state->calibration_candidate_start_time >= 0.0) && ((now - state->calibration_candidate_start_time) >= 0.35))
							{
								calibration_ready = true;
								state->tracking_space_calibration_pending = false;
							}
						}
					}

					if(!state->tracking_space_calibrated && calibration_ready && (state->session_state == XR_SESSION_STATE_FOCUSED))
					{
						const Vec3f xr_forward_engine = getXRForwardVectorInEngineSpace(head_pose.orientation);
						const float flat_len2 = xr_forward_engine.x * xr_forward_engine.x + xr_forward_engine.y * xr_forward_engine.y;
						const float xr_heading = (flat_len2 > 1.0e-6f) ? std::atan2(xr_forward_engine.y, xr_forward_engine.x) : current_world_heading;

						state->tracking_base_yaw_offset = current_world_heading - xr_heading;
						state->tracking_base_orientation_offset = buildNeutralWorldHeadOrientation(current_world_heading) * buildEngineSpaceQuatFromXROrientation(head_pose.orientation).conjugate();
						state->calibration_world_heading = current_world_heading;
						state->calibration_tracking_head_pos_engine = mapXRPositionToEngineSpace(head_pose.position);
						state->tracking_space_calibrated = true;
						state->tracking_space_calibration_pending = false;
						state->calibration_candidate_pose_valid = false;
						state->calibration_candidate_start_time = -1.0;
					}

					const float effective_yaw_offset = state->tracking_base_yaw_offset + (current_world_heading - state->calibration_world_heading);
					const Quatf effective_orientation_offset = normalise(
						Quatf::zAxisRot(current_world_heading - state->calibration_world_heading) * state->tracking_base_orientation_offset
					);
					const Vec3d anchor_pos_d = cam_controller.getFirstPersonPosition();
					const Vec3f anchor_pos((float)anchor_pos_d.x, (float)anchor_pos_d.y, (float)anchor_pos_d.z);
					const Vec3f world_translation = anchor_pos - rotateAroundWorldUp(state->calibration_tracking_head_pos_engine, effective_yaw_offset);

					head_pose_state.active = true;
					raw_head_pose_state.active = true;
					head_pose_state.position_valid = head_position_valid;
					head_pose_state.orientation_valid = head_orientation_valid;
					head_pose_state.position_tracked = head_position_tracked;
					head_pose_state.orientation_tracked = head_orientation_tracked;
					raw_head_pose_state.position_valid = head_position_valid;
					raw_head_pose_state.orientation_valid = head_orientation_valid;
					raw_head_pose_state.position_tracked = head_position_tracked;
					raw_head_pose_state.orientation_tracked = head_orientation_tracked;
					buildObjectToWorldMatrixFromXRPose(head_pose, Quatf::identity(), 0.f, Vec3f(0.f), raw_head_pose_state.object_to_world_matrix);
					buildObjectToWorldMatrixFromXRPose(head_pose, effective_orientation_offset, effective_yaw_offset, world_translation, head_pose_state.object_to_world_matrix);

					XREyeViewState* eye_states[2] = { &left_eye_view_state, &right_eye_view_state };
					const uint32_t eye_state_count = std::min<uint32_t>(last_result.located_view_count, 2u);
					for(uint32_t eye_i = 0; eye_i < eye_state_count; ++eye_i)
					{
						XREyeViewState& eye_state = *eye_states[eye_i];
						eye_state.raw_pose.active = true;
						eye_state.raw_pose.position_valid = position_valid;
						eye_state.raw_pose.orientation_valid = orientation_valid;
						eye_state.raw_pose.position_tracked = position_tracked;
						eye_state.raw_pose.orientation_tracked = orientation_tracked;
						eye_state.world_pose.active = true;
						eye_state.world_pose.position_valid = position_valid;
						eye_state.world_pose.orientation_valid = orientation_valid;
						eye_state.world_pose.position_tracked = position_tracked;
						eye_state.world_pose.orientation_tracked = orientation_tracked;
						buildObjectToWorldMatrixFromXRPose(state->views[eye_i].pose, Quatf::identity(), 0.f, Vec3f(0.f), eye_state.raw_pose.object_to_world_matrix);
						buildObjectToWorldMatrixFromXRPose(state->views[eye_i].pose, effective_orientation_offset, effective_yaw_offset, world_translation, eye_state.world_pose.object_to_world_matrix);

						const XrFovf& eye_fov = state->views[eye_i].fov;
						eye_state.fov_valid =
							isFinite(eye_fov.angleLeft) &&
							isFinite(eye_fov.angleRight) &&
							isFinite(eye_fov.angleUp) &&
							isFinite(eye_fov.angleDown);
						if(eye_state.fov_valid)
						{
							eye_state.angle_left_deg = radToDegree(eye_fov.angleLeft);
							eye_state.angle_right_deg = radToDegree(eye_fov.angleRight);
							eye_state.angle_up_deg = radToDegree(eye_fov.angleUp);
							eye_state.angle_down_deg = radToDegree(eye_fov.angleDown);
						}
					}

					if(last_result.actions_synced && last_result.action_spaces_created)
					{
						std::string action_error;
						uint32_t active_pose_count = 0;

						if(!locateActionSpace(state->hands[0].grip_space, state->app_space, frame_state.predictedDisplayTime, effective_orientation_offset, effective_yaw_offset, world_translation, left_hand_state.grip_pose, action_error))
							last_result.actions_message = "Left grip pose: " + action_error;
						else if(left_hand_state.grip_pose.active)
							active_pose_count++;

						if(!locateActionSpace(state->hands[0].aim_space, state->app_space, frame_state.predictedDisplayTime, effective_orientation_offset, effective_yaw_offset, world_translation, left_hand_state.aim_pose, action_error))
							last_result.actions_message = "Left aim pose: " + action_error;
						else if(left_hand_state.aim_pose.active)
							active_pose_count++;

						if(!locateActionSpace(state->hands[1].grip_space, state->app_space, frame_state.predictedDisplayTime, effective_orientation_offset, effective_yaw_offset, world_translation, right_hand_state.grip_pose, action_error))
							last_result.actions_message = "Right grip pose: " + action_error;
						else if(right_hand_state.grip_pose.active)
							active_pose_count++;

						if(!locateActionSpace(state->hands[1].aim_space, state->app_space, frame_state.predictedDisplayTime, effective_orientation_offset, effective_yaw_offset, world_translation, right_hand_state.aim_pose, action_error))
							last_result.actions_message = "Right aim pose: " + action_error;
						else if(right_hand_state.aim_pose.active)
							active_pose_count++;

						if(last_result.actions_message.empty())
							last_result.actions_message = "OpenXR actions synced; " + std::to_string(active_pose_count) + " hand pose space(s) active in the current frame.";
					}

					const GLuint prev_draw_fbo = FrameBuffer::getCurrentlyBoundDrawFrameBuffer();
					GLint prev_viewport[4] = { 0, 0, 0, 0 };
					glGetIntegerv(GL_VIEWPORT, prev_viewport);
					const Reference<FrameBuffer> old_target_frame_buffer = opengl_engine.getTargetFrameBuffer();
					const int old_main_viewport_w = opengl_engine.getMainViewPortWidth();
					const int old_main_viewport_h = opengl_engine.getMainViewPortHeight();

					opengl_engine.setNearDrawDistance(near_draw_dist);
					opengl_engine.setMaxDrawDistance(max_draw_dist);

					OpenGLScene* scene = opengl_engine.getCurrentScene();
					const bool old_render_to_main_render_framebuffer = scene ? scene->render_to_main_render_framebuffer : false;
					const bool old_collect_stats = scene ? scene->collect_stats : false;
					if(scene)
					{
						// In XR, render directly into the eye framebuffer to avoid desktop-style offscreen/MSAA resolves and viewport-size realloc churn.
						scene->render_to_main_render_framebuffer = false;
						scene->collect_stats = false;
					}

					projection_views.resize(last_result.located_view_count);
					bool rendered_all_views = true;

					for(uint32_t i=0; i<last_result.located_view_count; ++i)
					{
						OpaqueState::ViewSwapchain& view_swapchain = state->view_swapchains[i];

						float sensor_width = 0.f;
						float lens_sensor_dist = 0.f;
						float render_aspect_ratio = 0.f;
						float lens_shift_up = 0.f;
						float lens_shift_right = 0.f;
						if(!computeProjectionParamsFromFOV(state->views[i].fov, sensor_width, lens_sensor_dist, render_aspect_ratio, lens_shift_up, lens_shift_right))
						{
							last_result.message = "Could not derive projection parameters from xrLocateViews FOV for eye " + std::to_string(i) + ".";
							rendered_all_views = false;
							break;
						}

						uint32_t image_index = 0;
						bool image_acquired = false;

						clearOpenGLErrors();
						XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
						const XrResult acquire_res = xrAcquireSwapchainImage(view_swapchain.swapchain, &acquire_info, &image_index);
						if(XR_FAILED(acquire_res))
						{
							last_result.message = "xrAcquireSwapchainImage failed for eye " + std::to_string(i) + " with XrResult=" + std::to_string((int)acquire_res);
							rendered_all_views = false;
							break;
						}
						image_acquired = true;

						XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
						wait_info.timeout = XR_INFINITE_DURATION;
						const XrResult wait_res = xrWaitSwapchainImage(view_swapchain.swapchain, &wait_info);
						if(XR_FAILED(wait_res))
						{
							last_result.message = "xrWaitSwapchainImage failed for eye " + std::to_string(i) + " with XrResult=" + std::to_string((int)wait_res);
							if(image_acquired)
							{
								XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
								xrReleaseSwapchainImage(view_swapchain.swapchain, &release_info);
							}
							rendered_all_views = false;
							break;
						}

						if(image_index >= view_swapchain.framebuffers.size())
						{
							last_result.message = "OpenXR runtime returned an out-of-range swapchain image index for eye " + std::to_string(i) + ".";
							if(image_acquired)
							{
								XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
								xrReleaseSwapchainImage(view_swapchain.swapchain, &release_info);
							}
							rendered_all_views = false;
							break;
						}

						opengl_engine.setTargetFrameBufferAndViewport(view_swapchain.framebuffers[image_index]);
						opengl_engine.setMainViewportDims((int)view_swapchain.width, (int)view_swapchain.height);

						Matrix4f world_to_camera_space_matrix;
						buildWorldToCameraMatrixFromXRPose(state->views[i].pose, effective_orientation_offset, effective_yaw_offset, world_translation, world_to_camera_space_matrix);
						Matrix4f xr_projection_matrix;
						if(!buildOpenGLProjectionMatrixFromFOV(state->views[i].fov, near_draw_dist, xr_projection_matrix))
						{
							last_result.message = "Could not build OpenXR projection matrix from xrLocateViews FOV for eye " + std::to_string(i) + ".";
							if(image_acquired)
							{
								XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
								xrReleaseSwapchainImage(view_swapchain.swapchain, &release_info);
							}
							rendered_all_views = false;
							break;
						}
						if(i == 0)
						{
							last_mirror_view.valid = true;
							last_mirror_view.world_to_camera_space_matrix = world_to_camera_space_matrix;
							last_mirror_view.sensor_width = sensor_width;
							last_mirror_view.lens_sensor_dist = lens_sensor_dist;
							last_mirror_view.render_aspect_ratio = render_aspect_ratio;
							last_mirror_view.lens_shift_up = lens_shift_up;
							last_mirror_view.lens_shift_right = lens_shift_right;
							last_mirror_view.projection_matrix_override_valid = true;
							last_mirror_view.projection_matrix_override = xr_projection_matrix;
						}
						opengl_engine.setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, lens_shift_up, lens_shift_right);
						// Feed the runtime's exact per-eye frustum to the shader path, while keeping the existing camera/frustum setup for culling.
						opengl_engine.setProjectionMatrixOverride(xr_projection_matrix);
						opengl_engine.draw();
						opengl_engine.clearProjectionMatrixOverride();

						clearOpenGLErrors();
						XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
						const XrResult release_res = xrReleaseSwapchainImage(view_swapchain.swapchain, &release_info);
						if(XR_FAILED(release_res))
						{
							last_result.message = "xrReleaseSwapchainImage failed for eye " + std::to_string(i) + " with XrResult=" + std::to_string((int)release_res);
							rendered_all_views = false;
							break;
						}

						XrCompositionLayerProjectionView& projection_view = projection_views[i];
						std::memset(&projection_view, 0, sizeof(XrCompositionLayerProjectionView));
						projection_view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
						projection_view.pose = state->views[i].pose;
						projection_view.fov = state->views[i].fov;
						projection_view.subImage.swapchain = view_swapchain.swapchain;
						projection_view.subImage.imageRect.offset.x = 0;
						projection_view.subImage.imageRect.offset.y = 0;
						projection_view.subImage.imageRect.extent.width = (int32_t)view_swapchain.width;
						projection_view.subImage.imageRect.extent.height = (int32_t)view_swapchain.height;
						projection_view.subImage.imageArrayIndex = 0;
					}

					opengl_engine.setTargetFrameBuffer(old_target_frame_buffer);
					opengl_engine.clearProjectionMatrixOverride();
					opengl_engine.setMainViewportDims(old_main_viewport_w, old_main_viewport_h);
					opengl_engine.setViewportDims(prev_viewport[2], prev_viewport[3]);
					glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
					glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
					if(scene)
					{
						scene->render_to_main_render_framebuffer = old_render_to_main_render_framebuffer;
						scene->collect_stats = old_collect_stats;
					}

					if(rendered_all_views && !projection_views.empty())
					{
						projection_layer.space = state->app_space;
						projection_layer.viewCount = (uint32_t)projection_views.size();
						projection_layer.views = &projection_views[0];
						submitted_layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection_layer);
						submitted_layer_count = 1;
						last_result.projection_layer_submitted = true;
						last_result.message = "OpenXR projection layer submitted with " + std::to_string(projection_views.size()) + " stereo views.";
					}
				}
			}
			else if(last_result.located_view_count == 0)
			{
				last_result.message = "xrLocateViews succeeded but returned zero stereo views.";
			}
			else
			{
				last_result.message = "xrLocateViews succeeded, but pose validity flags were not set.";
			}
		}
		else
		{
			last_result.message = "xrLocateViews failed with XrResult=" + std::to_string((int)locate_views_res);
		}
	}

	XrFrameEndInfo frame_end_info = { XR_TYPE_FRAME_END_INFO };
	frame_end_info.displayTime = frame_state.predictedDisplayTime;
	frame_end_info.environmentBlendMode = state->environment_blend_mode;
	frame_end_info.layerCount = submitted_layer_count;
	frame_end_info.layers = (submitted_layer_count > 0) ? submitted_layers : NULL;

	clearOpenGLErrors();
	const XrResult end_frame_res = xrEndFrame(state->session, &frame_end_info);
	if(XR_FAILED(end_frame_res))
	{
		last_result.frame_loop_active = false;
		last_result.message = "xrEndFrame failed with XrResult=" + std::to_string((int)end_frame_res);
	}
#endif
}


bool XRSession::isInitialised() const
{
	return last_result.session_created && last_result.local_reference_space_created && last_result.swapchains_created;
}


void XRSession::shutdown()
{
#if defined(XR_SUPPORT)
	if(!state)
		return;

	destroyActionSubsystem(left_hand_state, right_hand_state, *state);

	for(size_t i=0; i<state->view_swapchains.size(); ++i)
	{
		if(state->view_swapchains[i].swapchain != XR_NULL_HANDLE)
		{
			xrDestroySwapchain(state->view_swapchains[i].swapchain);
			state->view_swapchains[i].swapchain = XR_NULL_HANDLE;
		}
	}
	destroyViewSwapchains(*state);

	if(state->session != XR_NULL_HANDLE)
	{
		if(state->view_space != XR_NULL_HANDLE)
		{
			xrDestroySpace(state->view_space);
			state->view_space = XR_NULL_HANDLE;
		}

		if(state->app_space != XR_NULL_HANDLE)
		{
			xrDestroySpace(state->app_space);
			state->app_space = XR_NULL_HANDLE;
		}

		xrDestroySession(state->session);
		state->session = XR_NULL_HANDLE;
	}

	if(state->instance != XR_NULL_HANDLE)
	{
		xrDestroyInstance(state->instance);
		state->instance = XR_NULL_HANDLE;
	}

	state->system_id = XR_NULL_SYSTEM_ID;
	state->session_state = XR_SESSION_STATE_UNKNOWN;
	state->session_running = false;
	state->view_config_views.clear();
	state->environment_blend_modes.clear();
	state->views.clear();
	state->environment_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	last_result.swapchains_created = false;
	last_result.projection_layer_submitted = false;
	last_result.action_set_created = false;
	last_result.pose_actions_created = false;
	last_result.input_actions_created = false;
	last_result.action_spaces_created = false;
	last_result.action_bindings_suggested = false;
	last_result.action_sets_attached = false;
	last_result.actions_synced = false;
	last_result.suggested_binding_profile_count = 0;
	last_result.actions_message.clear();
	last_mirror_view.valid = false;
	resetTrackedPoseState(head_pose_state);
	resetTrackedPoseState(raw_head_pose_state);
	resetEyeViewState(left_eye_view_state);
	resetEyeViewState(right_eye_view_state);
	std::memset(&state->view_state, 0, sizeof(XrViewState));
	state->view_state.type = XR_TYPE_VIEW_STATE;
#endif
}
