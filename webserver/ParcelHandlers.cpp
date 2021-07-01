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
		// Parse parcel id from request path
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

			// Lookup screenshot object for auction
			for(size_t z=0; z<parcel->screenshot_ids.size(); ++z)
			{
				const uint64 screenshot_id = parcel->screenshot_ids[z];

				auto shot_res = world_state.screenshots.find(screenshot_id);
				if(shot_res != world_state.screenshots.end())
				{
					Screenshot* shot = shot_res->second.ptr();
					if(shot->state == Screenshot::ScreenshotState_notdone)
						page += "<div style=\"display: inline-block;\">Screenshot processing...</div>     \n";
					else
						page += "<div style=\"display: inline-block;\"><a href=\"/screenshot/" + toString(screenshot_id) + "\"><img src=\"/screenshot/" + toString(screenshot_id) + "\" width=\"320px\" alt=\"screenshot\" /></a></div>   \n";
				}
			}

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

			const Vec3d span = parcel->aabb_max - parcel->aabb_min;
			page += "<p>Dimensions: " + toString(span.x) + " m x " + toString(span.y) + " m x " + toString(span.z) + " m.</p>   \n";

			const Vec3d centre = (parcel->aabb_max + parcel->aabb_min) * 0.5;
			const double dist_from_orig = centre.getDist(Vec3d(0, 0, 0));

			page += "<p>Location: x: " + toString((int)centre.x) + ", y: " + toString((int)centre.y) + " (" + doubleToStringNSigFigs(dist_from_orig, 2) + " m from the origin)</p>  \n";


			// Show NFT status
			page += "<h2>NFT status</h2>         \n";
			if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
				page += "<p>This parcel has not been minted as an NFT.</p>";
			else if(parcel->nft_status == Parcel::NFTStatus_MintingNFT)
				page += "<p>This parcel is being minted as an Ethereum NFT...</p>";
			else if(parcel->nft_status == Parcel::NFTStatus_MintedNFT)
			{
				auto txn_res = world_state.sub_eth_transactions.find(parcel->minting_transaction_id);
				if(txn_res != world_state.sub_eth_transactions.end())
				{
					const SubEthTransaction* trans = txn_res->second.ptr();
					page += "<p>This parcel has been minted as an Ethereum NFT.</p><p>View <a href=\"https://etherscan.io/tx/0x" + trans->transaction_hash.toHexString() + "\">minting transaction on Etherscan</a></p>";
				}
				else
				{
					page += "<p>This parcel has been minted as an Ethereum NFT.  (Could not find minting transaction hash)</p>";
				}
			}

			// Get current auction if any
			page += "<h2>Current auction</h2>         \n";
			const TimeStamp now = TimeStamp::currentTime();
			int num = 0;
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->currentlyForSale(now)) // If auction is valid and running:
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
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">" + auction->getAuctionEndOrSoldTime().timeDescription() + ": Parcel sold.</a> <br/>";
						num++;
					}
					else if(auction->auction_end_time <= now)
					{
						page += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">" + auction->getAuctionEndOrSoldTime().timeDescription() + ": Parcel did not sell.</a> <br/>";
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

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(logged_in_user && parcel->owner_id == logged_in_user->id) // If the user is logged in and owns this parcel:
			{
				page += "<h2>Parcel owner tools</h2>  \n";

				page += "<form action=\"/regenerate_parcel_screenshots\" method=\"post\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
				page += "<input type=\"submit\" value=\"Regenerate screenshots\"> (Screenshot generation may take several minutes)";
				page += "</form>";
			}


			if(LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
			{
				page += "<h2>Admin tools</h2>  \n";
				page += "<p><a href=\"/admin_set_parcel_owner/" + parcel->id.toString() + "\">Set parcel owner</a></p>";

				page += "<form action=\"/admin_regenerate_parcel_screenshots\" method=\"post\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
				page += "<input type=\"submit\" value=\"Regenerate screenshots\">";
				page += "</form>";
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


// URL for parcel ERC 721 metadata JSON
// See https://docs.opensea.io/docs/metadata-standards
void renderMetadata(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		// Parse parcel id from request path
		Parser parser(request.path.c_str(), request.path.size());
		if(!parser.parseString("/p/"))
			throw glare::Exception("Failed to parse /p/");

		uint32 parcel_id;
		if(!parser.parseUnsignedInt(parcel_id))
			throw glare::Exception("Failed to parse parcel id");

		std::string page = "{"
			"\"name\":\"Parcel " + toString(parcel_id) + "\","
			"\"external_url\":\"https://substrata.info/parcel/" + toString(parcel_id) + "\"," // "This is the URL that will appear below the asset's image on OpenSea and will allow users to leave OpenSea and view the item on your site."
			;

		{ // lock scope
			Lock lock(world_state.mutex);

			Reference<ServerWorldState> root_world = world_state.getRootWorldState();

			auto res = root_world->parcels.find(ParcelID(parcel_id));
			if(res == root_world->parcels.end())
				throw glare::Exception("Couldn't find parcel");

			const Parcel* parcel = res->second.ptr();

			if(!parcel->screenshot_ids.empty())
			{
				const uint64 screenshot_id = parcel->screenshot_ids[0];

				page += "\"image\":\"https://substrata.info/screenshot/" + toString(screenshot_id) + "\",";
			}

			std::string descrip;
			const Vec3d span = parcel->aabb_max - parcel->aabb_min;
			descrip += "Dimensions: " + toString(span.x) + " m x " + toString(span.y) + " m x " + toString(span.z) + " m.  "; // Two spaces make a linebreak in markdown (maybe)  "\n" doesn't work on OpenSea at least

			const Vec3d centre = (parcel->aabb_max + parcel->aabb_min) * 0.5;
			const double dist_from_orig = centre.getDist(Vec3d(0, 0, 0));

			descrip += "Location: x: " + toString((int)centre.x) + ", y: " + toString((int)centre.y) + " (" + doubleToStringNSigFigs(dist_from_orig, 2) + " m from the origin).";
			descrip += "  Visit https://substrata.info/parcel/" + toString(parcel_id) + " for more info.";

			page += "\"description\":\"" + web::Escaping::JSONEscape(descrip) + "\"" // "A human readable description of the item. Markdown is supported."
				"}";
		} // end lock scope

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page.c_str(), page.size(), "application/json");
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRegenerateParcelScreenshots(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && parcel->owner_id == logged_in_user->id) // If the user is logged in and owns this parcel:
				{
					for(size_t z=0; z<parcel->screenshot_ids.size(); ++z)
					{
						const uint64 screenshot_id = parcel->screenshot_ids[z];

						auto shot_res = world_state.screenshots.find(screenshot_id);
						if(shot_res != world_state.screenshots.end())
						{
							Screenshot* shot = shot_res->second.ptr();
							shot->state = Screenshot::ScreenshotState_notdone;
						}
					}

					world_state.markAsChanged();
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		conPrint("handleRegenerateParcelScreenshots error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace ParcelHandlers
