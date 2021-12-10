/*=====================================================================
WebServerResponseUtils.cpp
--------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "WebServerResponseUtils.h"


#include "../server/ServerWorldState.h"
#include "RequestInfo.h"
#include "Escaping.h"
#include "LoginHandlers.h"
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <Exception.h>
#include <MySocket.h>
#include <Lock.h>
#include <Clock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <maths/Rect2.h>


namespace WebServerResponseUtils
{


const std::string CRLF = "\r\n";


const std::string standardHTMLHeader(const web::RequestInfo& request_info, const std::string& page_title, const std::string& extra_header_tags)
{
	return
		"	<!DOCTYPE html>																									\n"
		"	<html>																											\n"
		"		<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">		\n"
		"		<title>" + web::Escaping::HTMLEscape(page_title) + "</title>												\n"
		"		<link href=\"/files/main.css\" rel=\"stylesheet\" />														\n"
		+ extra_header_tags + 
		"		</head>																										\n";
}


const std::string standardHeader(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, const std::string& page_title, const std::string& extra_header_tags)
{
	std::string page_out = standardHTMLHeader(request_info, page_title, extra_header_tags);
	page_out +=
		"	<body style=\"margin-top: 60px;\">\n"
		"	<div id=\"login\" style=\"float: right; margin-top: -8px;\">\n"; // Start login div
	
	web::UnsafeString logged_in_username;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username);

	if(logged_in)
	{
		page_out += "You are logged in as <a href=\"/account\">" + logged_in_username.HTMLEscaped() + "</a>";

		if(logged_in_username == "Ono-Sendai")
			page_out += " | <a href=\"/admin\">Admin page</a>\n";

		// Add logout button
		page_out += "<form action=\"/logout_post\" method=\"post\">\n";
		page_out += "<input class=\"link-button\" type=\"submit\" value=\"Log out\">\n";
		page_out += "</form>\n";
	}
	else
	{
		page_out += "<a href=\"/login\">log in</a> <br/>\n";
	}
	page_out += 
	"	</div>																									\n" // End login div
	"	<a href=\"/\"><img src=\"/files/logo_small.png\" alt=\"substrata logo\" style=\"padding-bottom:20px\"/></a>											\n"
	
	"	<header>																								\n"
	"		<h1>" + web::Escaping::HTMLEscape(page_title) + "</h1>												\n"
	"	</header>																								\n";
		
	return page_out;
}


const std::string standardFooter(const web::RequestInfo& request_info, bool include_email_link)
{
	std::string page_out;
	page_out +=
		"	<hr/>																						\n"
		"	<div class=\"footer\" style=\"font-size: 0.8em; color: grey\">Substrata is made by <a href=\"http://glaretechnologies.com\">Glare Technologies</a>		\n"
		"	Contact us at contact@glaretechnologies.com<br/>											\n"
		"	</div>																						\n"
		"	<div class=\"footer\" style=\"font-size: 0.8em; color: grey; text-align: center\"><a href=\"/faq\">F.A.Q.</a> | <a href=\"/terms\">Terms of use</a> | <a href=\"/bot_status\">Bot status</a> | <a href=\"/map\">Map</a></div>				\n"
		"	</body>																						\n"
		"</html>																						\n";

	return page_out;
}


const std::string getMapHeaderTags()
{
	return "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.7.1/dist/leaflet.css\"\
		integrity=\"sha512-xodZBNTC5n17Xt2atTPuE1HxjVMSvLVW9ocqUKLsCC5CXdbqCmblAshOMAS6/keqq/sMZMZ19scR4PsZChSR7A==\"\
		crossorigin=\"\"/>";
}


const std::string getMapEmbedCode(ServerAllWorldsState& world_state, ParcelID highlighted_parcel_id)
{
	std::string page;
	page += 
		"<script src=\"https://unpkg.com/leaflet@1.7.1/dist/leaflet.js\"\
		integrity=\"sha512-XQoYMqMTK8LvdxXYG3nZ448hOEQiglfqkJs1NOQV44cWnUrBc8PkAOcXy20w0vlaXaVUearIOBhiXZ5V3ynxwA==\"\
		crossorigin=\"\"></script>";

	page += "<a name=\"map\"></a>";
	page += "<div style=\"height: 650px\" id=\"mapid\"></div>";

	// Get parcel polygon boundaries.  Some parcels are rectangles, so we will handle those as a special case optimisation where we can just write a rectangle.
	std::vector<Vec2d> poly_verts;
	std::vector<int> poly_parcel_ids;
	std::vector<int> poly_parcel_state; // 0 = owned by MrAdmin and not on auction, 1 = owned by MrAdmin and for auction, 2 = owned by someone else

	std::vector<Rect2d> rect_bounds;
	std::vector<int> rect_parcel_ids;
	std::vector<int> rect_parcel_state;

	const TimeStamp now = TimeStamp::currentTime();

	{ // lock scope
		Lock lock(world_state.mutex);

		ServerWorldState* root_world = world_state.getRootWorldState().ptr();


		poly_verts.reserve(44 * 4);
		poly_parcel_ids.reserve(44);
		poly_parcel_state.reserve(44);

		rect_bounds.reserve(root_world->parcels.size());
		rect_parcel_ids.reserve(root_world->parcels.size());
		rect_parcel_state.reserve(root_world->parcels.size());

		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();

			int state;
			if(parcel->owner_id.value() == 0)
			{
				state = 0;
				
				// See if this parcel is currently up for auction
				if(!parcel->parcel_auction_ids.empty())
				{
					const uint32 auction_id = parcel->parcel_auction_ids.back();
					auto auction_res = world_state.parcel_auctions.find(auction_id);
					if(auction_res != world_state.parcel_auctions.end())
					{
						const ParcelAuction* auction = auction_res->second.ptr();
						if(auction->currentlyForSale(now)) // If auction is valid and running:
							state = 1;
					}
				}
			}
			else
				state = 2;

			if(parcel->isAxisAlignedBox())
			{
				rect_bounds.push_back(Rect2d(Vec2d(parcel->aabb_min.x, parcel->aabb_min.y), Vec2d(parcel->aabb_max.x, parcel->aabb_max.y)));
				rect_parcel_ids.push_back((int)parcel->id.value());
				rect_parcel_state.push_back(state);
			}
			else
			{
				for(int i=0; i<4; ++i)
					poly_verts.push_back(parcel->verts[i]);

				poly_parcel_ids.push_back((int)parcel->id.value());
				poly_parcel_state.push_back(state);
			}
		}
	} // End lock scope

	const double scale = 1.0 / 20; // Not totally sure where this scale comes from, but somehow from const float TILE_WIDTH_M = 5120.f / (1 << tile_z);
	std::string var_js;
	var_js.reserve(rect_parcel_ids.size() * 80); // Reserve 80 chars per parcel (only use about 70 in practice).
	var_js += "<script>poly_coords = [";
	for(size_t i=0; i<poly_verts.size(); ++i)
	{
		var_js += "[" + doubleToStringMaxNDecimalPlaces(poly_verts[i].y * scale, 2) + ", " + doubleToStringMaxNDecimalPlaces(poly_verts[i].x * scale, 2) + "]";
		if(i + 1 < poly_verts.size())
			var_js += ",\n";
	}
	var_js += "];\n";

	var_js += "poly_parcel_ids = [";
	for(size_t i=0; i<poly_parcel_ids.size(); ++i)
	{
		var_js += toString(poly_parcel_ids[i]);
		if(i + 1 < poly_parcel_ids.size())
			var_js += ", ";
	}
	var_js += "];\n";

	var_js += "poly_parcel_state = [";
	for(size_t i=0; i<poly_parcel_state.size(); ++i)
	{
		var_js += toString(poly_parcel_state[i]);
		if(i + 1 < poly_parcel_state.size())
			var_js += ", ";
	}
	var_js += "];\n";
	
	var_js += "rect_bound_coords = [";
	for(size_t i=0; i<rect_bounds.size(); ++i)
	{
		var_js += 
			doubleToStringMaxNDecimalPlaces(rect_bounds[i].getMin().y * scale, 2) + ", " + doubleToStringMaxNDecimalPlaces(rect_bounds[i].getMin().x * scale, 2) + "," + 
			doubleToStringMaxNDecimalPlaces(rect_bounds[i].getMax().y * scale, 2) + ", " + doubleToStringMaxNDecimalPlaces(rect_bounds[i].getMax().x * scale, 2);

		if(i + 1 < rect_bounds.size())
			var_js += ",\n";
	}
	var_js += "];\n";

	var_js += "rect_parcel_ids = [";
	for(size_t i=0; i<rect_parcel_ids.size(); ++i)
	{
		var_js += toString(rect_parcel_ids[i]);
		if(i + 1 < rect_parcel_ids.size())
			var_js += ", ";
	}
	var_js += "];\n";

	var_js += "rect_parcel_state = [";
	for(size_t i=0; i<rect_parcel_state.size(); ++i)
	{
		var_js += toString(rect_parcel_state[i]);
		if(i + 1 < rect_parcel_state.size())
			var_js += ", ";
	}
	var_js += "];\n";

	var_js += "highlight_parcel_id = " + toString(highlighted_parcel_id.value()) + ";";

	var_js += "</script>";

	page += var_js;

	page += "<script src=\"/files/map.js\"></script>";

	return page;
}


} // end namespace WebServerResponseUtils
