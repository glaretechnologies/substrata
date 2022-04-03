/*=====================================================================
ImageDecoding.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../utils/Reference.h"
#include "../utils/Exception.h"
#include <string>
#include <vector>
class Map2D;


/*=====================================================================
ImageDecoding
---------------

=====================================================================*/
class ImageDecoding
{
public:

	static Reference<Map2D> decodeImage(const std::string& indigo_base_dir, const std::string& path);

	static bool hasSupportedImageExtension(const std::string& path);
};
