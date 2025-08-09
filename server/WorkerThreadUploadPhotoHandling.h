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
Handles photo uploads on the server side.
=====================================================================*/
namespace WorkerThreadUploadPhotoHandling
{

void handlePhotoUploadConnection(Reference<SocketInterface> socket, Server* server, const web::RequestInfo& websocket_request_info, bool fuzzing);

void saveMidSizeAndThumbnailImages(const std::string& src_full_res_screenshot_filename, const std::string& random_path_hex_str, const std::string& photo_dir, const std::vector<uint8>& src_image_data, 
	std::string& midsize_filename_out, std::string& thumbnail_filename_out);

void test();

};
