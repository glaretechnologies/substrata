/*=====================================================================
AdminHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AdminHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <Parser.h>


namespace AdminHandlers
{


bool loggedInUserHasAdminPrivs(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	web::UnsafeString logged_in_username;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username);
	return logged_in && (logged_in_username.str() == "Ono-Sendai");
}


void renderMainAdminPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Main Admin Page");
	
	if(!loggedInUserHasAdminPrivs(world_state, request_info))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	{ // Lock scope
		Lock lock(world_state.mutex);
		
		// Print out users
		page_out += "<h2>Users</h2>\n";

		for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			page_out += "<div>\n";
			page_out += "id: " + user->id.toString() + ",       username: " + web::Escaping::HTMLEscape(user->name) + ",       email: " + web::Escaping::HTMLEscape(user->email_address) + ",      joined " + user->created_time.timeAgoDescription();
			page_out += "</div>\n";
		}

		/*page_out += "<table>";
		for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			page_out += "<tr>\n";
			page_out += "<td>" + user->id.toString() + "</td><td>" + web::Escaping::HTMLEscape(user->name) + "</td><td>" + web::Escaping::HTMLEscape(user->email_address) + "</td><td>" + user->created_time.timeAgoDescription() + "</td>";
			page_out += "</tr>\n";
		}
		page_out += "</table>";*/


		page_out += "<h2>Parcel auctions</h2>\n";

		page_out += "<div><a href=\"/parcel_auction_list\">Parcel auction list</a></div>";

		// Print out parcels
		page_out += "<h2>Root world Parcels</h2>\n";

		Reference<ServerWorldState> root_world = world_state.getRootWorldState();
		if(root_world.isNull())
			return;

		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();

			// Look up owner
			std::string owner_username;
			auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			if(user_res == world_state.user_id_to_users.end())
				owner_username = "[No user found]";
			else
				owner_username = user_res->second->name;


			page_out += "<div>\n";
			page_out += "<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a>,       owner: " + web::Escaping::HTMLEscape(owner_username) + ",       description: " + web::Escaping::HTMLEscape(parcel->description) +
				",      created " + parcel->created_time.timeAgoDescription();
			
			// Get any auctions for parcel
			page_out += "<div>    \n";
			if(!parcel->parcel_auction_ids.empty())
			{
				const uint32 auction_id = parcel->parcel_auction_ids.back();
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
						page_out += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">For sale at auction</a>";
					else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
						page_out += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">Parcel sold.</a>";
				}
			}
			page_out += "</div>    \n";

			page_out += " <a href=\"/admin_create_parcel_auction/" + parcel->id.toString() + "\">Create auction</a>";

			page_out += "</div>\n";
			page_out += "<br/>  \n";
		}

	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderCreateParcelAuction(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse parcel id from request path
	Parser parser(request.path.c_str(), request.path.size());
	if(!parser.parseString("/admin_create_parcel_auction/"))
		throw glare::Exception("Failed to parse /admin_create_parcel_auction/");

	uint32 id;
	if(!parser.parseUnsignedInt(id))
		throw glare::Exception("Failed to parse parcel id");
	const ParcelID parcel_id(id);


	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request, "Sign Up");

	page_out += "<body>";
	page_out += "</head><h1>Create Parcel Auction</h1><body>";

	page_out += "<form action=\"/admin_create_parcel_auction_post\" method=\"post\">";
	page_out += "parcel id: <input type=\"number\" name=\"parcel_id\" value=\"" + parcel_id.toString() + "\"><br>";
	page_out += "auction start time: <input type=\"number\" name=\"auction_start_time\"   value=\"0\"> hours from now<br>";
	page_out += "auction end time:   <input type=\"number\" name=\"auction_end_time\"     value=\"72\"> hours from now<br>";
	page_out += "auction start price: <input type=\"number\" name=\"auction_start_price\" value=\"1000\"> EUR<br/>";
	page_out += "auction end price: <input type=\"number\" name=\"auction_end_price\"     value=\"50\"> EUR<br/>";
	page_out += "<input type=\"submit\" value=\"Create auction\">";
	page_out += "</form>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void createParcelAuctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		// Try and log in user
		const uint64 parcel_id              = stringToUInt64(request.getPostField("parcel_id").str());
		const double auction_start_time_hrs = stringToDouble(request.getPostField("auction_start_time").str());
		const double auction_end_time_hrs   = stringToDouble(request.getPostField("auction_end_time").str());
		const double auction_start_price    = stringToDouble(request.getPostField("auction_start_price").str());
		const double auction_end_price      = stringToDouble(request.getPostField("auction_end_price").str());

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				// Found user for username
				Parcel* parcel = res->second.ptr();

				ParcelAuctionRef auction = new ParcelAuction();
				auction->id = (uint32)world_state.parcel_auctions.size() + 1;
				auction->parcel_id = parcel->id;
				auction->auction_state = ParcelAuction::AuctionState_ForSale;
				auction->auction_start_time  = TimeStamp((uint64)(TimeStamp::currentTime().time + auction_start_time_hrs * 3600));
				auction->auction_end_time    = TimeStamp((uint64)(TimeStamp::currentTime().time + auction_end_time_hrs   * 3600));
				auction->auction_start_price = auction_start_price;
				auction->auction_end_price   = auction_end_price;

				world_state.parcel_auctions[auction->id] = auction;

				parcel->parcel_auction_ids.push_back(auction->id);

				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_auction/" + toString(auction->id));
			}
		} // End lock scope

	}
	catch(glare::Exception& e)
	{
		conPrint("handleLoginPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}



} // end namespace AdminHandlers
