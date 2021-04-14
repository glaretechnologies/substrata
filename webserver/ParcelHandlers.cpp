/*=====================================================================
ParcelHandlers.cpp
-------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ParcelHandlers.h"



#include "RequestInfo.h"
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include "../server/Order.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <Parser.h>


namespace ParcelHandlers
{


void renderParcelPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows order details
{
	try
	{
		

		// Parse order id from request path
		Parser parser(request.path.c_str(), request.path.size());
		if(!parser.parseString("/parcel/"))
			throw glare::Exception("Failed to parse /parcel/");

		uint32 parcel_id;
		if(!parser.parseUnsignedInt(parcel_id))
			throw glare::Exception("Failed to parse parcel id");

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Parcel #" + toString(parcel_id) + "");
		page += "<div class=\"main\">   \n";

		{ // lock scope
			Lock lock(world_state.mutex);

			Reference<ServerWorldState> root_world = world_state.getRootWorldState();

			auto res = root_world->parcels.find(ParcelID(parcel_id));
			if(res == root_world->parcels.end())
				throw glare::Exception("Couldn't find parcel");

			const Parcel* parcel = res->second.ptr();

			//page += "<div>Parcel " + parcel->id.toString() + "</div>";

			const Vec3d pos = parcel->getVisitPosition();
			page += "<p>Visit in Substrata: <span style=\"color: blue\">sub://substrata.info/?x=" + doubleToStringNSigFigs(pos.x, 2) + "&y=" + doubleToStringNSigFigs(pos.y, 2) + "&z=" + doubleToStringNSigFigs(pos.z, 2) +
				"</span><br/>(enter URL into location bar in Substrata client)</p>   \n";

			// Look up owner
			std::string owner_username;
			auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			if(user_res == world_state.user_id_to_users.end())
				owner_username = "[No user found]";
			else
				owner_username = user_res->second->name;


			page += "<p>Owner: " + web::Escaping::HTMLEscape(owner_username) + "</p>   \n";
			page += "<p>Description: " + web::Escaping::HTMLEscape(parcel->description) + "</p>   \n";
			//page += "<p>Created: " + parcel->created_time.timeAgoDescription() + "</p>   \n";

			// Get current auction if any
			page += "<h2>Current auction</h2>         \n";
			int num = 0;
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
					{
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">Parcel for sale, auction ends " + auction->auction_end_time.timeDescription() + "</a>";
						num++;
					}
				}
			}
			if(num == 0)
				page += "This parcel is not currently for sale.";

			page += "<h2>Past auctions</h2>         \n";
			num = 0;
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->auction_state == ParcelAuction::AuctionState_Sold)
					{
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">" + auction->auction_end_time.timeDescription() + ": Parcel sold.</a> <br/>";
						num++;
					}
					else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
					{
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">" + auction->auction_end_time.timeDescription() + ": Parcel did not sell.</a> <br/>";
						num++;
					}
				}
			}

			if(num == 0)
				page += "There are no past auctions for this parcel.";



			/*if(!parcel->parcel_auction_ids.empty())
			{
				const uint32 auction_id = parcel->parcel_auction_ids.back();
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
						page += " <a href=\"/parcel_auction/" + toString(auction_id) + "\">For sale - view auction</a>.";
					else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">Parcel sold.</a>";
				}
			}*/
			page += "</p>";

			if(LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
			{
				page += "<h3>Admin tools</h3>  \n";
				page += "<p><a href=\"/admin_set_parcel_owner/" + parcel->id.toString() + "\">Set parcel owner</a></p>";
			}

		} // end lock scope

		page += "</div>   \n"; // end main div
		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace ParcelHandlers
