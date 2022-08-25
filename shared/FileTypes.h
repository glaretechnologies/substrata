/*=====================================================================
FileTypes.h
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../utils/StringUtils.h"


/*=====================================================================
FileTypes
---------

=====================================================================*/
class FileTypes
{
public:
	static bool hasAudioFileExtension(const std::string& url)
	{
		return hasExtensionStringView(url, "mp3") || hasExtensionStringView(url, "m4a") || hasExtensionStringView(url, "wav") || hasExtensionStringView(url, "aac") || hasExtensionStringView(url, "flac");
	}

	static inline bool hasSupportedVideoFileExtension(const std::string& url)
	{
		return hasExtensionStringView(url, "mp4");
	}

	static bool hasSupportedExtension(const std::string& path);
};
