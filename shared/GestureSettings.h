/*=====================================================================
GestureSettings.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "URLString.h"
#include <string>
#include <vector>
class RandomAccessInStream;
class RandomAccessOutStream;


/*=====================================================================
GestureSettings
---------------

=====================================================================*/

class SingleGestureSettings
{
public:
	SingleGestureSettings() : flags(0), anim_duration(1.0) {}
	void writeToStream(RandomAccessOutStream& stream) const;

	std::string friendly_name; // e.g. "Left Turn".  For display to the user in the UI.  Also should be the name in the AnimationDatum in the animation file.
	URLString anim_URL; // A .subanim URL, e.g. Left_Turn_2343532535.subanim
	uint32 flags;
	float anim_duration; // So we can easily untoggle button when gesture has finished.

	static const size_t MAX_NAME_SIZE = 1000;

	static const uint32 FLAG_ANIMATE_HEAD = 1; // Should the animation data control the head (e.g. override the procedural lookat anim)?
	static const uint32 FLAG_LOOP         = 2; // Should the animation automatically loop?
};


class GestureSettings
{
public:
	static GestureSettings defaultGestureSettings();

	void writeToStream(RandomAccessOutStream& stream) const;

	static bool isDefaultGestureName(const std::string& name);


	static const size_t MAX_GESTURE_SETTINGS_SIZE = 1000;

	std::vector<SingleGestureSettings> gesture_settings;
};


void readSingleGestureSettingsFromStream(RandomAccessInStream& stream, SingleGestureSettings& settings);

void readGestureSettingsFromStream(RandomAccessInStream& stream, GestureSettings& settings);
