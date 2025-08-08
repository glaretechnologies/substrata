/*=====================================================================
WorkerThreadUploadPhotoHandling.h
---------------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <RequestInfo.h>
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <MySocket.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include <BufferInStream.h>
#include <AtomicInt.h>
#include <string>
#include <vector>
class Server;


/*=====================================================================
WorkerThreadUploadPhotoHandling
-------------------------------
=====================================================================*/


namespace WorkerThreadUploadPhotoHandling
{

void handlePhotoUploadConnection(Reference<SocketInterface> socket, Server* server, const web::RequestInfo& websocket_request_info, bool fuzzing);

void saveMidSizeAndThumbnailImages(const std::string& src_full_res_screenshot_filename, const std::string& random_path_hex_str, const std::string& photo_dir, const std::vector<uint8>& src_image_data, 
	std::string& midsize_filename_out, std::string& thumbnail_filename_out);

void test();

};
