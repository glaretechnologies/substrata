/*=====================================================================
WorkerThreadUploadPhotoHandling.h
---------------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <RequestInfo.h>
#include <Platform.h>
#include <string>
#include <vector>
class SocketInterface;
class Server;


/*=====================================================================
WorkerThreadUploadPhotoHandling
-------------------------------
Handles photo and video uploads on the server side.
=====================================================================*/
namespace WorkerThreadUploadPhotoHandling
{

// For ConnectionTypeUploadPhoto, and ConnectionTypeUploadVideo (is_video = true), where the uploaded JPEG is a poster frame and is followed by the MP4 data.
void handlePhotoUploadConnection(Reference<SocketInterface> socket, Server* server, const web::RequestInfo& websocket_request_info, bool fuzzing, bool is_video);

void saveMidSizeAndThumbnailImages(const std::string& src_full_res_screenshot_filename, const std::string& random_path_hex_str, const std::string& photo_dir, const std::vector<uint8>& src_image_data, 
	std::string& midsize_filename_out, std::string& thumbnail_filename_out);

void test();

};
