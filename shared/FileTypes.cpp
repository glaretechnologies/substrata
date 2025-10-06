/*=====================================================================
FileTypes.cpp
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "FileTypes.h"


#include "../gui_client/ModelLoading.h"
#include "ImageDecoding.h"


bool FileTypes::hasSupportedExtension(const string_view path)
{
	return
		ModelLoading::hasSupportedModelExtension(path) ||
		ImageDecoding::hasSupportedImageExtension(path) ||
		hasAudioFileExtension(path) ||
		hasSupportedVideoFileExtension(path);		
}
