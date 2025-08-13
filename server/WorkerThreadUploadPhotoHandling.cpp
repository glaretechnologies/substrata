/*=====================================================================
WorkerThreadUploadPhotoHandling.cpp
-----------------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "WorkerThreadUploadPhotoHandling.h"


#include "ServerWorldState.h"
#include "Server.h"
#include "../webserver/LoginHandlers.h"
#include "../shared/Protocol.h"
#include <graphics/jpegdecoder.h>
#include <vec3.h>
#include <ConPrint.h>
#include <Exception.h>
#include <SocketInterface.h>
#include <URL.h>
#include <Lock.h>
#include <StringUtils.h>
#include <CryptoRNG.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <FileUtils.h>
#include <MemMappedFile.h>
#include <FileChecksum.h>
#include <maths/CheckedMaths.h>
#include <RuntimeCheck.h>
#include <Timer.h>
#include <utils/TestUtils.h>


namespace WorkerThreadUploadPhotoHandling
{


static const int MAX_STRING_LEN = 10000;


void handlePhotoUploadConnection(Reference<SocketInterface> socket, Server* server, const web::RequestInfo& websocket_request_info, bool fuzzing)
{
	conPrint("handlePhotoUploadConnection()");

	socket->flush();

	try
	{
		const std::string username = socket->readStringLengthFirst(MAX_STRING_LEN);
		const std::string password = socket->readStringLengthFirst(MAX_STRING_LEN);

		UserID client_user_id = UserID::invalidUserID();
		std::string client_user_name;
		{
			Lock lock(server->world_state->mutex);
			auto res = server->world_state->name_to_users.find(username);
			if(res != server->world_state->name_to_users.end())
			{
				User* user = res->second.getPointer();
				if(user->isPasswordValid(password))
				{
					// Password is valid, log user in.
					client_user_id = user->id;
					client_user_name = user->name;
				}
			}
		}

		// If the client connected via a websocket, they can be logged in with a session cookie.
		// Note that this may only work if the websocket connects over TLS.
		{
			Lock lock(server->world_state->mutex);
			User* cookie_logged_in_user = LoginHandlers::getLoggedInUser(*server->world_state, websocket_request_info);
			if(cookie_logged_in_user != NULL)
			{
				conPrint("handleResourceUploadConnection: logged in via cookie.");
				client_user_id   = cookie_logged_in_user->id;
				client_user_name = cookie_logged_in_user->name;
			}
		}


		SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);


		if(!client_user_id.valid())
		{
			conPrint("\tLogin failed.");
			socket->writeUInt32(Protocol::LogInFailure); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Login failed.");

			socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			socket->flush();
			return;
		}

		if(server->world_state->isInReadOnlyMode())
		{
			conPrint("\tin read only-mode..");
			socket->writeUInt32(Protocol::ServerIsInReadOnlyMode); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Server is in read-only mode, can't upload files.");

			socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			socket->flush();
			return;
		}
		

		// Read world name
		const std::string world_name = socket->readStringLengthFirst(Photo::MAX_WORLD_NAME_SIZE);

		// Read parcel id
		const ParcelID parcel_id = readParcelIDFromStream(*socket);

		// Read cam_pos, cam_angles
		Vec3d cam_pos, cam_angles;
		socket->readData(&cam_pos, sizeof(cam_pos));
		socket->readData(&cam_angles, sizeof(cam_angles));

		// Read caption
		const std::string caption = socket->readStringLengthFirst(Photo::MAX_CAPTION_SIZE);


		// Generate random path
		const int NUM_BYTES = 16;
		uint8 pathdata[NUM_BYTES];
		CryptoRNG::getRandomBytes(pathdata, NUM_BYTES);
		const std::string random_path_hex_str = StringUtils::convertByteArrayToHexString(pathdata, NUM_BYTES);
		const std::string photo_filename = "photo_" + random_path_hex_str + ".jpg";
		const std::string photo_path = server->photo_dir + "/" + photo_filename;


		// Read photo data
		const uint64 data_len = socket->readUInt64();
		if(data_len > 20'000'000) // ~20MB
		{
			socket->writeUInt32(Protocol::PhotoUploadFailed);
			socket->writeStringLengthFirst("Photo was too large (max size is 20 MB)");
			return;
		}

		conPrint("Receiving photo of " + toString(data_len) + " B");
		std::vector<uint8> data(data_len);
		socket->readData(data.data(), data_len);

		conPrint("Received photo of " + toString(data_len) + " B");

		std::string midsize_filename, thumbnail_filename;

		// Save photo to path
		if(!fuzzing) // Don't write to disk while fuzzing
		{
			// Make other photo sizes
			try
			{
				saveMidSizeAndThumbnailImages(/*src_full_res_screenshot_filename=*/photo_filename, random_path_hex_str, server->photo_dir, data, 
					/*midsize_filename_out=*/midsize_filename, /*thumbnail_filename_out=*/thumbnail_filename);
			}
			catch(glare::Exception& e)
			{
				conPrint("handlePhotoUploadConnection: glare::Exception while making thumbnail for photo: " + e.what());

				socket->writeUInt32(Protocol::PhotoUploadFailed);
				socket->writeStringLengthFirst("Server error: failed to make thumbnail for photo.");
				socket->flush();
				return;
			}

			try
			{
				// Save original/full resolution photo to disk.  Do this after making other photo sizes, which will check it's a valid JPEG file.
				FileUtils::writeEntireFile(photo_path, data);
			}
			catch(glare::Exception& e)
			{
				conPrint("handlePhotoUploadConnection: glare::Exception while saving photo to disk: " + e.what());

				socket->writeUInt32(Protocol::PhotoUploadFailed);
				socket->writeStringLengthFirst("Server error: failed to save photo.");
				socket->flush();
				return;
			}
		}

		conPrint("Saved to disk at " + photo_path);

		PhotoRef photo = new Photo();
		photo->id = server->world_state->getNextPhotoUID();
		photo->creator_id = client_user_id;
		photo->parcel_id = parcel_id;
		photo->created_time = TimeStamp::currentTime();
		photo->cam_pos = cam_pos;
		photo->cam_angles = cam_angles;
		photo->caption = caption;
		photo->world_name = world_name;
		photo->local_filename = photo_filename;
		photo->local_thumbnail_filename = thumbnail_filename;
		photo->local_midsize_filename = midsize_filename;

		// Add to server DB
		{
			Lock lock(server->world_state->mutex);
			server->world_state->photos.insert(std::make_pair(photo->id, photo));
			server->world_state->addPhotoAsDBDirty(photo);
		}

		socket->writeUInt32(Protocol::PhotoUploadSucceeded);
		socket->flush();
	}
	catch(glare::Exception& e)
	{
		conPrint("handlePhotoUploadConnection: glare::Exception: " + e.what());
	}
	catch(std::exception& e)
	{
		conPrint(std::string("handlePhotoUploadConnection: Caught std::exception: ") + e.what());
	}
}


void saveMidSizeAndThumbnailImages(const std::string& src_full_res_screenshot_filename, const std::string& random_path_hex_str, const std::string& photo_dir, const std::vector<uint8>& src_image_data, std::string& midsize_filename_out, std::string& thumbnail_filename_out)
{
	conPrint("WorkerThreadUploadPhotoHandling::saveMidSizeAndThumbnailImages()");

	const std::string base_dir_path = "."; // not used
	Map2DRef im = JPEGDecoder::decodeFromBuffer(src_image_data.data(), src_image_data.size(), base_dir_path);
	if(!im.isType<ImageMapUInt8>())
		throw glare::Exception("decoded image was not ImageMapUInt8");

	if((im->getMapWidth() < 8) || (im->getMapHeight() < 8))
		throw glare::Exception("image too small.");


	//------------------------ Save midsize image (if needed) --------------------------
	// max width: 1000px
	{
		if(im->getMapWidth() > 1000)
		{
			// Resize needed
			const int midsize_width = 1000;
			const int midsize_height = (int)(1000.0 * (double)im->getMapHeight() / (double)im->getMapWidth());

			// Do the resize
			Map2DRef resized = im->resizeMidQuality(midsize_width, midsize_height, /*task_manager=*/nullptr);
			if(!resized)
				throw glare::Exception("resizeMidQuality failed.");
			if(!resized.isType<ImageMapUInt8>())
				throw glare::Exception("resized image was not ImageMapUInt8");

			// Save to disk
			const std::string midsize_filename = "photo_" + random_path_hex_str + "_midsize1000.jpg";
			const std::string midsize_path = photo_dir + "/" + midsize_filename;

			conPrint("Saving resized midsize image to disk at '" + midsize_path + "'...");

			JPEGDecoder::save(resized.downcast<ImageMapUInt8>(), midsize_path, JPEGDecoder::SaveOptions(/*quality=*/95));

			midsize_filename_out = midsize_filename;
		}
		else
		{
			// If src full-res image is small enough already, just use it directly.

			conPrint("Just using src_full_res_screenshot_filename for midsize_filename_out: '" + src_full_res_screenshot_filename + "'.");
			midsize_filename_out = src_full_res_screenshot_filename;
		}
	}


	//------------------------ Save thumbnail --------------------------
	// Thumbnail is 230 px wide with a fixed aspect ratio of 4/3.
	// The source image will be cropped to get the desired aspect ratio.
	{
		// Crop to a certain ratio
		const int thumb_width = 230;
		const int thumb_height = 230 * 3 / 4; // 4/3 ratio

		int cropped_w, cropped_h;
		if((double)im->getMapWidth() / (double)im->getMapHeight()  > ((double)thumb_width / (double)thumb_height))
		{
			// Image is wider than thumbnail ratio, need to crop off left and right edges.
			cropped_w = (int)((double)im->getMapHeight() * ((double)thumb_width / (double)thumb_height));
			cropped_h = (int)im->getMapHeight();
		}
		else
		{
			// Image is taller than thumbnail ratio, need to crop off top and bottom edges.
			cropped_w = (int)im->getMapWidth();
			cropped_h = (int)((double)im->getMapWidth() * ((double)thumb_height / (double)thumb_width));
		}

		// Crop the image to the correct ratio
		const int left_right_edge_w = ((int)im->getMapWidth()  - cropped_w) / 2; // Get the width of the edge that will be cropped off, if any.
		const int top_bottom_edge_w = ((int)im->getMapHeight() - cropped_h) / 2;
		ImageMapUInt8Ref cropped = im.downcast<ImageMapUInt8>()->cropImage(/*start_x=*/left_right_edge_w, /*start_y=*/top_bottom_edge_w, cropped_w, cropped_h);

		// Do the resize
		Map2DRef resized = cropped->resizeMidQuality(thumb_width, thumb_height, /*task_manager=*/nullptr);
		if(!resized)
			throw glare::Exception("resizeMidQuality failed.");
		if(!resized.isType<ImageMapUInt8>())
			throw glare::Exception("resized image was not ImageMapUInt8");

		// Save to disk
		const std::string thumbnail_filename = "photo_" + random_path_hex_str + "_thumb_" + toString(thumb_width) + "x" + toString(thumb_height) + ".jpg";
		const std::string thumbnail_path = photo_dir + "/" + thumbnail_filename;

		conPrint("Saving thumbnail image to disk at '" + thumbnail_path + "'...");

		JPEGDecoder::save(resized.downcast<ImageMapUInt8>(), thumbnail_path, JPEGDecoder::SaveOptions(/*quality=*/95));

		thumbnail_filename_out = thumbnail_filename;
	}

	conPrint("WorkerThreadUploadPhotoHandling::saveMidSizeAndThumbnailImages() done.");
}


#if BUILD_TESTS
void test()
{
	try
	{
		{
			const std::string src_full_res_screenshot_filename = "italy_bolsena_flag_flowers_stairs_01.jpg";
			const std::string src_full_res_screenshot_path = TestUtils::getTestReposDir() + "/testfiles/" + src_full_res_screenshot_filename;
			std::vector<uint8> src_image_data;
			FileUtils::readEntireFile(TestUtils::getTestReposDir() + "/testfiles/italy_bolsena_flag_flowers_stairs_01.jpg", src_image_data);

			std::string thumbnail_filename, midsize_filename;
			saveMidSizeAndThumbnailImages(src_full_res_screenshot_path, /*random_path_hex_str=*/"aaa", /*photo_dir=*/"d:/files", src_image_data, midsize_filename, thumbnail_filename);
		}
		{
			const std::string src_full_res_screenshot_filename = "screenshot_1560x1028.jpg";
			const std::string src_full_res_screenshot_path = TestUtils::getTestReposDir() + "/testfiles/jpegs/" + src_full_res_screenshot_filename;
			std::vector<uint8> src_image_data;
			FileUtils::readEntireFile(TestUtils::getTestReposDir() + "/testfiles/jpegs/screenshot_1560x1028.jpg", src_image_data);

			std::string midsize_filename, thumbnail_filename;
			saveMidSizeAndThumbnailImages(src_full_res_screenshot_path, /*random_path_hex_str=*/"bbb", /*photo_dir=*/"d:/files", src_image_data, midsize_filename, thumbnail_filename);
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}
#endif


} // end namespace WorkerThreadUploadPhotoHandling
