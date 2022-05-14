/*=====================================================================
ImageDecoding.cpp
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "ImageDecoding.h"


#include "../utils/StringUtils.h"
#include <stdlib.h>//for NULL
#include <fstream>
#include "../maths/mathstypes.h"
#include <graphics/jpegdecoder.h>
#include <graphics/PNGDecoder.h>
#include <graphics/TIFFDecoder.h>
#include <graphics/EXRDecoder.h>
#include <graphics/GifDecoder.h>
#include <graphics/KTXDecoder.h>
#include "../graphics/Map2D.h"


Reference<Map2D> ImageDecoding::decodeImage(const std::string& indigo_base_dir, const std::string& path) // throws ImFormatExcep on failure
{
	if(hasExtension(path, "jpg") || hasExtension(path, "jpeg"))
	{
		return JPEGDecoder::decode(indigo_base_dir, path);
	}
	else if(hasExtension(path, "png"))
	{
		return PNGDecoder::decode(path);
	}
	// Disable TIFF loading until we fuzz it etc.
	/*else if(hasExtension(path, "tif") || hasExtension(path, "tiff"))
	{
		return TIFFDecoder::decode(path);
	}*/
	else if(hasExtension(path, "exr"))
	{
		return EXRDecoder::decode(path);
	}
	else if(hasExtension(path, "gif"))
	{
		return GIFDecoder::decode(path);
	}
	else if(hasExtension(path, "ktx"))
	{
		return KTXDecoder::decode(path);
	}
	else if(hasExtension(path, "ktx2"))
	{
		return KTXDecoder::decodeKTX2(path);
	}
	else
	{
		throw glare::Exception("Unhandled image format ('" + getExtension(path) + "')");
	}
}


bool ImageDecoding::hasSupportedImageExtension(const std::string& path)
{
	return
		hasExtension(path, "jpg") || hasExtension(path, "jpeg") ||
		hasExtension(path, "png") ||
		//hasExtension(path, "tif") || hasExtension(path, "tiff") ||
		hasExtension(path, "exr") ||
		hasExtension(path, "gif") ||
		hasExtension(path, "ktx") || 
		hasExtension(path, "ktx2");
}
