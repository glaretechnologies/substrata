/*=====================================================================
XRSupport.cpp
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "XRSupport.h"


namespace
{
#if defined(EMSCRIPTEN)
static constexpr bool kXRBuildEnabled = false;
static constexpr bool kXRNativeBuild = false;
static constexpr bool kXRRuntimeCapableBuild = false;
static constexpr const char* kXRBackendName = "None";
static constexpr const char* kXRStatusDescription = "XR is unavailable in the Emscripten/web build.";
#elif defined(XR_SUPPORT)
static constexpr bool kXRBuildEnabled = true;
static constexpr bool kXRNativeBuild = true;
static constexpr bool kXRRuntimeCapableBuild = true;
static constexpr const char* kXRBackendName = "OpenXR";
static constexpr const char* kXRStatusDescription = "XR support is enabled in the build. The Win32 Qt/OpenGL client now has OpenXR runtime probing, session lifecycle, stereo swapchains, per-eye projection rendering, projection-layer submit, a Qt companion mirror preview, and a first backend-only OpenXR action layer with grip/aim hand poses plus trigger/select diagnostics; gameplay input mapping remains ahead.";
#else
static constexpr bool kXRBuildEnabled = false;
static constexpr bool kXRNativeBuild = true;
static constexpr bool kXRRuntimeCapableBuild = false;
static constexpr const char* kXRBackendName = "None";
static constexpr const char* kXRStatusDescription = "XR support is not compiled in. Configure CMake with -DXR_SUPPORT=ON after installing the OpenXR SDK and a desktop OpenXR runtime.";
#endif
}


namespace XR
{
bool isBuildTimeEnabled()
{
	return kXRBuildEnabled;
}


bool isNativeBuild()
{
	return kXRNativeBuild;
}


bool isRuntimeCapableBuild()
{
	return kXRRuntimeCapableBuild;
}


const char* backendName()
{
	return kXRBackendName;
}


const char* statusDescription()
{
	return kXRStatusDescription;
}
}
