/*=====================================================================
ImageDecoding.cpp
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "ImageDecoding.h"


#include <graphics/jpegdecoder.h>
#include <graphics/PNGDecoder.h>
#include <graphics/TIFFDecoder.h>
#include <graphics/EXRDecoder.h>
#include <graphics/GifDecoder.h>
#include <graphics/KTXDecoder.h>
#include <graphics/BasisDecoder.h>
#include <graphics/Map2D.h>
#include <utils/StringUtils.h>
#include <utils/MemMappedFile.h>
#include <maths/mathstypes.h>
#include <stdlib.h> // for NULL
#include <fstream>


Reference<Map2D> ImageDecoding::decodeImage(const std::string& indigo_base_dir, const std::string& path, glare::Allocator* mem_allocator, const ImageDecodingOptions& options) // throws ImFormatExcep on failure
{
	if(hasExtension(path, "jpg") || hasExtension(path, "jpeg") ||
		hasExtension(path, "png") ||
		hasExtension(path, "exr") ||
		hasExtension(path, "gif") ||
		hasExtension(path, "ktx") ||
		hasExtension(path, "ktx2") ||
		hasExtension(path, "basis"))
	{
		MemMappedFile file(path);
		return decodeImageFromBuffer(indigo_base_dir, path, ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize()), mem_allocator, options);
	}
	else
	{
		throw glare::Exception("Unhandled image format ('" + getExtension(path) + "')");
	}
}


Reference<Map2D> ImageDecoding::decodeImageFromBuffer(const std::string& indigo_base_dir, const std::string& path, ArrayRef<uint8> texture_data_buf, glare::Allocator* mem_allocator, const ImageDecodingOptions& options)
{
	if(hasExtension(path, "jpg") || hasExtension(path, "jpeg"))
	{
		return JPEGDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), indigo_base_dir, mem_allocator);
	}
	else if(hasExtension(path, "png"))
	{
		return PNGDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), mem_allocator);
	}
	// Disable TIFF loading until we fuzz it etc.
	/*else if(hasExtension(path, "tif") || hasExtension(path, "tiff"))
	{
		return TIFFDecoder::decode(path);
	}*/
	else if(hasExtension(path, "exr"))
	{
		return EXRDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), path, mem_allocator);
	}
	else if(hasExtension(path, "gif"))
	{
		return GIFDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), mem_allocator);
	}
	else if(hasExtension(path, "ktx"))
	{
		return KTXDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), mem_allocator);
	}
	else if(hasExtension(path, "ktx2"))
	{
		return KTXDecoder::decodeKTX2FromBuffer(texture_data_buf.data(), texture_data_buf.size(), mem_allocator);
	}
	else if(hasExtension(path, "basis"))
	{
		BasisDecoder::BasisDecoderOptions basis_options;
		basis_options.ETC_support = options.ETC_support;
		return BasisDecoder::decodeFromBuffer(texture_data_buf.data(), texture_data_buf.size(), mem_allocator, basis_options);
	}
	else
	{
		throw glare::Exception("Unhandled image format ('" + getExtension(path) + "')");
	}
}


bool ImageDecoding::isSupportedImageExtension(string_view extension)
{
	return
		StringUtils::equalCaseInsensitive(extension, "jpg") || StringUtils::equalCaseInsensitive(extension, "jpeg") ||
		StringUtils::equalCaseInsensitive(extension, "png") ||
		//hasExtension(path, "tif") || hasExtension(path, "tiff") ||
		StringUtils::equalCaseInsensitive(extension, "exr") ||
		StringUtils::equalCaseInsensitive(extension, "gif") ||
		StringUtils::equalCaseInsensitive(extension, "ktx") ||
		StringUtils::equalCaseInsensitive(extension, "ktx2") ||
		StringUtils::equalCaseInsensitive(extension, "basis");
}


bool ImageDecoding::hasSupportedImageExtension(string_view path)
{
	const string_view extension = getExtensionStringView(path);

	return isSupportedImageExtension(extension);
}


static bool firstNBytesMatch(const void* data, size_t data_len, const void* target, size_t target_len)
{
	if(data_len < target_len)
		return false;

	return std::memcmp(data, target, target_len) == 0;
}


// Check if the first N bytes of the file data has the correct file signature / magic bytes, for the file type given by the extension.
// Most data from https://en.wikipedia.org/wiki/List_of_file_signatures
// File types handled are supported image types as given by isSupportedImageExtension() above.
bool ImageDecoding::areMagicBytesValid(const void* data, size_t data_len, string_view extension)
{
	if(StringUtils::equalCaseInsensitive(extension, "jpg") || StringUtils::equalCaseInsensitive(extension, "jpeg"))
	{
		const uint8 magic_bytes[] = { 0xFF, 0xD8, 0xFF };
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "png"))
	{
		const uint8 magic_bytes[] = { 0x89, 0x50, 0x4E, 0x47 };
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "exr"))
	{
		const uint8 magic_bytes[] = { 0x76, 0x2F, 0x31, 0x01 };
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "gif"))
	{
		const uint8 magic_bytes[] = { 0x47, 0x49, 0x46, 0x38 };
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "ktx"))
	{
		const uint8 magic_bytes[] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A }; // https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "ktx2"))
	{
		const uint8 magic_bytes[] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A }; // https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else if(StringUtils::equalCaseInsensitive(extension, "basis"))
	{
		const uint8 magic_bytes[] = { 0x73, 0x42, 0x13, 0x00 }; // Basis files in testfiles have these bytes in common.
		return firstNBytesMatch(data, data_len, magic_bytes, staticArrayNumElems(magic_bytes));
	}
	else
		return false;
}
