/*=====================================================================
PhotoHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "PhotoHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "WebDataStore.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "WorldHandlers.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <functional>


namespace PhotoHandlers
{


const std::string& get_local_filename(const Photo& p)           { return p.local_filename; }
const std::string& get_local_thumbnail_filename(const Photo& p) { return p.local_thumbnail_filename; }
const std::string& get_local_midsize_filename(const Photo& p)   { return p.local_midsize_filename; }


void doHandlePhotoImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info, 
	const std::string& path_prefix, // e.g. "/photo_midsize_image/"
	const std::function<const std::string& (const Photo&)>& get_field_func) // Get local_filename or local_thumbnail_filename or whatever from a Photo.
{
	try
	{
		// Parse photo id from request path
		Parser parser(request.path);
		if(!parser.parseString(path_prefix)) // parse e.g. "/photo_midsize_image/"
			throw glare::Exception("Failed to parse '" + path_prefix + "'");

		uint64 photo_id;
		if(!parser.parseUInt64(photo_id))
			throw glare::Exception("Failed to parse photo_id");

		// Get photo local_filename or local_midsize_filename or whatever
		std::string local_filename;
		bool photo_was_deleted = false;
		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.photos.find(photo_id);
			if(res == world_state.photos.end())
				throw glare::Exception("Couldn't find photo");

			const Photo* photo = res->second.ptr();
			if(photo->state != Photo::State_published)
				photo_was_deleted = true;

			local_filename = get_field_func(*photo);
		} // end lock scope

		if(photo_was_deleted)
		{
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Photo was deleted.");
			return;
		}

		const std::string local_path = datastore.photo_dir + "/" + local_filename;

		try
		{
			MemMappedFile file(local_path);
			
			assert(web::ResponseUtils::getContentTypeForPath(local_path) == "image/jpeg");
			const std::string content_type = "image/jpeg";

			// Send it to client
			web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheMaxAge(reply_info, file.fileData(), file.fileSize(), content_type, 3600*24*52); // cache max age = 1 year
		}
		catch(glare::Exception& e)
		{
			conPrint("handlePhotoImageRequest() exception:" + e.what());
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load photo.");
		}
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handlePhotoImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	doHandlePhotoImageRequest(world_state, datastore, request, reply_info, "/photo_image/", get_local_filename);
}


void handlePhotoMidSizeImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	doHandlePhotoImageRequest(world_state, datastore, request, reply_info, "/photo_midsize_image/", get_local_midsize_filename);
}


void handlePhotoThumbnailImageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	doHandlePhotoImageRequest(world_state, datastore, request, reply_info, "/photo_thumb_image/", get_local_thumbnail_filename);
}


void handlePhotoPageRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		// Parse photo id from request path
		Parser parser(request.path);
		if(!parser.parseString("/photo/"))
			throw glare::Exception("Failed to parse /photo/");

		uint64 photo_id;
		if(!parser.parseUInt64(photo_id))
			throw glare::Exception("Failed to parse photo_id");


		std::string page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*//*"Photo #" + toString(photo_id) + ""*/"");
		page += "<div class=\"main\">   \n";


		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			Reference<ServerWorldState> root_world = world_state.getRootWorldState();

			auto res = world_state.photos.find(photo_id);
			if(res == world_state.photos.end())
				throw glare::Exception("Couldn't find photo");

			const Photo* photo = res->second.ptr();

			if(photo->state == Photo::State_deleted)
			{
				page += "Photo has been deleted.";
			}
			else
			{
				// Insert image tag
				const std::string fullsize_image_URL = "/photo_image/" + toString(photo->id);
				const std::string midsize_image_URL = "/photo_midsize_image/" + toString(photo->id);
				page += "<a href=\"" + fullsize_image_URL + "\"><img src=\"" + midsize_image_URL + "\"  class=\"photo-midsize-img\"/></a>"; // width=\"800px\"

				page += "<figcaption><i>" + web::Escaping::HTMLEscape(photo->caption) + "</i></figcaption>\n";

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				const bool logged_in_user_is_photo_owner = logged_in_user && (photo->creator_id == logged_in_user->id); // If the user is logged in and owns this parcel:
				const bool logged_in_user_is_god_user = logged_in_user && isGodUser(logged_in_user->id);

				if(logged_in_user)
				{
					const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
					if(!msg.empty())
						page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
				}

				// Look up owner username
				{
					std::string owner_username;
					auto user_res = world_state.user_id_to_users.find(photo->creator_id);
					if(user_res == world_state.user_id_to_users.end())
						owner_username = "[No user found]";
					else
						owner_username = user_res->second->name;

					page += "<p>Photo by <i>" + web::Escaping::HTMLEscape(owner_username) + "</i></p>   \n";
				}

				// Look up parcel it was taken in, if any
				std::string parcel_descr;
				if(photo->parcel_id.valid())
				{
					auto parcel_res = root_world->parcels.find(photo->parcel_id);
					if(parcel_res != root_world->parcels.end())
					{
						const Parcel* parcel = parcel_res->second.ptr();
						parcel_descr = parcel->description.substr(0, 100);
					}
				}

				page += "<p>Taken " + photo->created_time.dayAndTimeStringUTC() + "</p>   \n";

				page += "<p>Location: ";
				if(photo->parcel_id.valid())
				{
					if(parcel_descr.empty())
						page += "<a href=\"/parcel/" + toString(photo->parcel_id.value()) + "\">Parcel " + toString(photo->parcel_id.value()) + "</a>";
					else
						page += "<a href=\"/parcel/" + toString(photo->parcel_id.value()) + "\">" + web::Escaping::HTMLEscape(parcel_descr) + "</a>";
				}
				else
					page += "<a href=\"/world/" + WorldHandlers::URLEscapeWorldName(photo->world_name) + "\">" + (photo->world_name.empty() ? "Main world" : web::Escaping::HTMLEscape(photo->world_name)) + "</a>";

				page += ", x: " + toString((int)photo->cam_pos.x) + ", y: " + toString((int)photo->cam_pos.y) + "</p>  \n";


				// See GUIClient::getCurrentWebClientURLPath()
				const Vec3d pos = photo->cam_pos;
				const std::string hostname = request.getHostHeader(); // Find the hostname the request was sent to
				const double heading_deg = Maths::doubleMod(::radToDegree(photo->cam_angles.x), 360.0);

				const std::string pos_and_heading_part = "x=" + doubleToStringMaxNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringMaxNDecimalPlaces(pos.y, 1) + "&z=" + doubleToStringMaxNDecimalPlaces(pos.z, 2) + 
					"&heading=" + doubleToStringNDecimalPlaces(heading_deg, 1);

				const std::string sub_URL = "sub://" + hostname + "/" + WorldHandlers::URLEscapeWorldName(photo->world_name) + "?" + pos_and_heading_part;
				
				const std::string webclient_URL = (request.tls_connection ? std::string("https") : std::string("http")) + "://" + hostname + "/webclient?world=" + WorldHandlers::URLEscapeWorldName(photo->world_name) + 
					"&" + pos_and_heading_part;

				page += "<p><a href=\"" + webclient_URL + "\">Visit location in web browser</a></p>";
				page += "<p><a href=\"" + sub_URL + "\">Visit location in native app</a></p>";

				if(logged_in_user_is_photo_owner || logged_in_user_is_god_user)
				{
					page += "<form action=\"/delete_photo_post\" method=\"post\" id=\"usrform\">";
					page += "<input type=\"hidden\" name=\"photo_id\" value=\"" + toString(photo->id) + "\"><br>";
					page += "<input type=\"submit\" class=\"delete-photo\" value=\"Delete photo\"\">"; // photo.js adds a confirm dialog to this button.
					page += "</form>";
				}
			}
		} // end lock scope

		page += "</div>   \n"; // end main div

		page += "<script src=\"/files/photo.js\"></script>";

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
		
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeletePhotoPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int photo_id = request.getPostIntField("photo_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.photos.find(photo_id);
			if(res != world_state.photos.end())
			{
				Photo* photo = res->second.ptr();

				const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && ((photo->creator_id == logged_in_user->id) || isGodUser(logged_in_user->id))) // If the user is logged in and created this photo, or if this is the god/superadmin user:
				{
					world_state.setUserWebMessage(logged_in_user->id, "Deleted photo.");

					photo->state = Photo::State_deleted;

					world_state.addPhotoAsDBDirty(photo);
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/photo/" + toString(photo_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeletePhotoPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace PhotoHandlers
