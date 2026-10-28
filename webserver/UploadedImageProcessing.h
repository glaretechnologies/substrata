/*=====================================================================
UploadedImageProcessing.h
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <ArrayRef.h>
#include <string>
class WebDataStore;


/*=====================================================================
UploadedImageProcessing
-----------------------
Processing of images uploaded by users (e.g. images for news posts and events),
which are saved in public_images_dir and served from /images/<filename>.
=====================================================================*/
namespace UploadedImageProcessing
{


struct ImageSizeLimits
{
	size_t max_file_size; // Max size of the uploaded file in bytes.
	int max_dim; // Max width or height of the uploaded image.
	int max_output_width; // Wider images are scaled down to this width.
	int thumbnail_width;
	int thumbnail_height;
};


struct SavedImage
{
	std::string image_filename; // Filename in public_images_dir
	std::string thumbnail_filename; // Filename in public_images_dir
	size_t width, height; // Dimensions of the saved image.
	size_t jpeg_size_B; // Size of the saved image file.
};


// Decodes an uploaded JPEG or PNG image, scales it down if it's wider than limits.max_output_width, re-encodes it as a JPEG,
// and saves it in public_images_dir, named by filename_prefix, the sanitised filename stem and a hash of the JPEG data.
// Re-encoding means only valid images are saved, and gives consistent, reasonably sized files.
// Also saves a thumbnail, cropped to the aspect ratio limits.thumbnail_width / limits.thumbnail_height.
// The filename extension gives the image format.  Throws glare::Exception if the data is not a valid image of a supported format.
SavedImage processAndSaveUploadedImage(WebDataStore& web_data_store, const std::string& filename, ArrayRef<uint8> data, const std::string& filename_prefix, const ImageSizeLimits& limits);


} // end namespace UploadedImageProcessing
