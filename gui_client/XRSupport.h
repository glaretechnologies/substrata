/*=====================================================================
XRSupport.h
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


namespace XR
{
bool isBuildTimeEnabled();
bool isNativeBuild();
bool isRuntimeCapableBuild();
const char* backendName();
const char* statusDescription();
}
