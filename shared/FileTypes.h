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
	static bool isSupportedAudioFileExtension(string_view extension)
	{
		return
			StringUtils::equalCaseInsensitive(extension, "mp3") ||
			StringUtils::equalCaseInsensitive(extension, "m4a") ||
			StringUtils::equalCaseInsensitive(extension, "wav") ||
			StringUtils::equalCaseInsensitive(extension, "aac") ||
			StringUtils::equalCaseInsensitive(extension, "flac");
	}

	static bool hasAudioFileExtension(const std::string& url)
	{
		const string_view extension = getExtensionStringView(url);
		return isSupportedAudioFileExtension(extension);
	}


	static inline bool isSupportedVideoFileExtension(string_view extension)
	{
		return StringUtils::equalCaseInsensitive(extension, "mp4");
	}

	static inline bool hasSupportedVideoFileExtension(const std::string& url)
	{
		return hasExtensionStringView(url, "mp4");
	}

	static bool hasSupportedExtension(const std::string& path);
};
