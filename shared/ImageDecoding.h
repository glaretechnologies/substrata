/*=====================================================================
ImageDecoding.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../utils/Reference.h"
#include "../utils/Exception.h"
#include "../utils/string_view.h"
#include <string>
#include <vector>
class Map2D;
namespace glare { class Allocator; }


/*=====================================================================
ImageDecoding
---------------

=====================================================================*/
class ImageDecoding
{
public:

	static Reference<Map2D> decodeImage(const std::string& indigo_base_dir, const std::string& path, glare::Allocator* mem_allocator = NULL);

	static bool isSupportedImageExtension(string_view extension);

	static bool hasSupportedImageExtension(const std::string& path);

	static bool areMagicBytesValid(const void* data, size_t data_len, string_view extension);
};
