/*=====================================================================
FileTypes.cpp
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "FileTypes.h"


#include "../gui_client/ModelLoading.h"
#include "ImageDecoding.h"


bool FileTypes::hasSupportedExtension(const std::string& path)
{
	return
		ModelLoading::hasSupportedModelExtension(path) ||
		ImageDecoding::hasSupportedImageExtension(path) ||
		hasAudioFileExtension(path) ||
		hasSupportedVideoFileExtension(path);		
}
