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
			StringUtils::equalCaseInsensitive(extension, "wav");
	}

	static bool hasAudioFileExtension(const string_view url)
	{
		const string_view extension = getExtensionStringView(url);
		return isSupportedAudioFileExtension(extension);
	}


	static inline bool isSupportedVideoFileExtension(string_view extension)
	{
		return StringUtils::equalCaseInsensitive(extension, "mp4");
	}

	static inline bool hasSupportedVideoFileExtension(const string_view url)
	{
		return hasExtension(url, "mp4");
	}

	static bool hasSupportedExtension(const string_view path);
};
