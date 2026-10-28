/*=====================================================================
UploadedImageProcessing.cpp
---------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "UploadedImageProcessing.h"


#include "WebDataStore.h"
#include "../shared/ImageDecoding.h"
#include <graphics/jpegdecoder.h>
#include <graphics/ImageMap.h>
#include <BufferOutStream.h>
#include <FileUtils.h>
#include <IncludeXXHash.h>
#include <Lock.h>
#include <Exception.h>
#include <StringUtils.h>


namespace UploadedImageProcessing
{


// Returns the filename with the extension removed, and any characters other than letters, digits and underscores replaced with underscores.  Truncated to 40 chars.
static std::string sanitisedFilenameStem(const std::string& filename)
{
	std::string stem = ::removeDotAndExtension(filename).substr(0, 40);
	for(size_t i=0; i<stem.size(); ++i)
		if(!(::isAlphaNumeric(stem[i]) || (stem[i] == '_')))
			stem[i] = '_';
	return stem.empty() ? "image" : stem;
}


static ImageMapUInt8Ref resizeImage(const ImageMapUInt8Ref& image, int new_w, int new_h)
{
	Map2DRef resized = image->resizeMidQuality(new_w, new_h, /*task_manager=*/nullptr);
	if(!resized.isType<ImageMapUInt8>())
		throw glare::Exception("Resized image was not ImageMapUInt8");
	return resized.downcast<ImageMapUInt8>();
}


// Encodes the image as a JPEG and saves it in public_images_dir, named by a hash of the JPEG data.  Returns the filename.
static std::string saveImageAsJPEG(WebDataStore& web_data_store, const ImageMapUInt8Ref& image, const std::string& filename_prefix, size_t& jpeg_size_out)
{
	BufferOutStream jpeg_data;
	JPEGDecoder::saveToStream(image, JPEGDecoder::SaveOptions(/*quality=*/90), jpeg_data);

	const uint64 hash = XXH64(jpeg_data.buf.data(), jpeg_data.buf.size(), /*seed=*/1);
	const std::string image_filename = filename_prefix + "_" + toString(hash) + ".jpg";
	const std::string path = web_data_store.public_images_dir + "/" + image_filename;

	if(!FileUtils::fileExists(path)) // If the same image was uploaded before, the file will already exist.
		FileUtils::writeEntireFileAtomically(path, (const char*)jpeg_data.buf.data(), jpeg_data.buf.size());

	{
		Lock lock(web_data_store.mutex);
		web_data_store.public_image_filenames.insert(image_filename); // So the image is served from /images/ straight away.
	}

	jpeg_size_out = jpeg_data.buf.size();
	return image_filename;
}


SavedImage processAndSaveUploadedImage(WebDataStore& web_data_store, const std::string& filename, ArrayRef<uint8> data, const std::string& filename_prefix, const ImageSizeLimits& limits)
{
	if(data.size() > limits.max_file_size)
		throw glare::Exception("Image file is too large (max " + toString(limits.max_file_size) + " bytes).");

	// The filename extension gives the image format.  Check the data matches it.
	const string_view extension = ::getExtensionStringView(filename);
	if(!(StringUtils::equalCaseInsensitive(extension, "jpg") || StringUtils::equalCaseInsensitive(extension, "jpeg") || StringUtils::equalCaseInsensitive(extension, "png")))
		throw glare::Exception("Image must be a JPEG or PNG file, with a .jpg, .jpeg or .png filename extension.");
	if(!ImageDecoding::areMagicBytesValid(data.data(), data.size(), extension))
		throw glare::Exception("Image data is not a valid file of the type given by the filename extension.");

	// Limit the number of pixels, so the decoder rejects too-large images before allocating memory for them.
	ImageDecoding::ImageDecodingOptions decoding_options;
	decoding_options.max_num_pixels = (size_t)limits.max_dim * (size_t)limits.max_dim;

	Map2DRef im = ImageDecoding::decodeImageFromBuffer(/*indigo_base_dir=*/".", filename, data, /*mem_allocator=*/NULL, decoding_options);

	if(!im.isType<ImageMapUInt8>())
		throw glare::Exception("Unsupported image format (16-bit images are not supported).");
	ImageMapUInt8Ref image = im.downcast<ImageMapUInt8>();

	if((image->getWidth() < 1) || (image->getHeight() < 1) || (image->getWidth() > (size_t)limits.max_dim) || (image->getHeight() > (size_t)limits.max_dim))
		throw glare::Exception("Invalid image size (max " + toString(limits.max_dim) + " pixels wide or high).");

	if(image->getN() == 4)
		image = image->extract3ChannelImage(); // Remove alpha, as JPEGs don't support it.
	else if((image->getN() != 1) && (image->getN() != 3))
		throw glare::Exception("Unsupported number of image channels: " + toString(image->getN()));

	if(image->getWidth() > (size_t)limits.max_output_width)
	{
		const int new_h = myMax(1, (int)((double)limits.max_output_width * image->getHeight() / image->getWidth()));
		image = resizeImage(image, limits.max_output_width, new_h);
	}

	const std::string stem = sanitisedFilenameStem(filename);

	SavedImage saved;
	saved.image_filename = saveImageAsJPEG(web_data_store, image, /*filename_prefix=*/filename_prefix + "_" + stem, saved.jpeg_size_B);
	saved.width  = image->getWidth();
	saved.height = image->getHeight();

	// Make the thumbnail: crop the centre of the image to the thumbnail aspect ratio, then scale it down to the thumbnail size if it's larger.
	const int w = (int)image->getWidth();
	const int h = (int)image->getHeight();
	const int crop_w = myClamp((int)((double)h * limits.thumbnail_width  / limits.thumbnail_height), 1, w);
	const int crop_h = myClamp((int)((double)w * limits.thumbnail_height / limits.thumbnail_width),  1, h);
	ImageMapUInt8Ref thumbnail = image->cropImage(/*start_x=*/(w - crop_w) / 2, /*start_y=*/(h - crop_h) / 2, crop_w, crop_h);
	if(crop_w > limits.thumbnail_width)
		thumbnail = resizeImage(thumbnail, limits.thumbnail_width, limits.thumbnail_height);

	size_t thumbnail_jpeg_size;
	saved.thumbnail_filename = saveImageAsJPEG(web_data_store, thumbnail, /*filename_prefix=*/filename_prefix + "_thumb_" + stem, thumbnail_jpeg_size);

	return saved;
}


} // end namespace UploadedImageProcessing
