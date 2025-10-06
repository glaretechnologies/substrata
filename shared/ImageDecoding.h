/*=====================================================================
ImageDecoding.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../utils/Reference.h"
#include "../utils/Exception.h"
#include "../utils/string_view.h"
#include "../utils/ArrayRef.h"
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

	struct ImageDecodingOptions
	{
		ImageDecodingOptions() : ETC_support(false) {}
		bool ETC_support;
	};

	// Decode image from disk
	static Reference<Map2D> decodeImage(const std::string& indigo_base_dir, const std::string& path, glare::Allocator* mem_allocator = NULL, const ImageDecodingOptions& options = ImageDecodingOptions());

	static Reference<Map2D> decodeImageFromBuffer(const std::string& indigo_base_dir, const std::string& path, ArrayRef<uint8> texture_data_buf, glare::Allocator* mem_allocator = NULL, const ImageDecodingOptions& options = ImageDecodingOptions());

	static bool isSupportedImageExtension(string_view extension);

	static bool hasSupportedImageExtension(string_view path);

	static bool areMagicBytesValid(const void* data, size_t data_len, string_view extension);
};
