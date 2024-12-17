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
#include <ContainerUtils.h>


namespace ParcelHandlers
{


void renderParcelPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows order details
{
	try
	{
		// Parse parcel id from request path
		Parser parser(request.path);
		if(!parser.parseString("/parcel/"))
			throw glare::Exception("Failed to parse /parcel/");

		uint32 parcel_id;
		if(!parser.parseUnsignedInt(parcel_id))
			throw glare::Exception("Failed to parse parcel id");

		const std::string extra_header_tags = WebServerResponseUtils::getMapHeaderTags();

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Parcel #" + toString(parcel_id) + "", extra_header_tags);
		page += "<div class=\"main\">   \n";

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			Reference<ServerWorldState> root_world = world_state.getRootWorldState();

			auto res = root_world->getParcels(lock).find(ParcelID(parcel_id));
			if(res == root_world->getParcels(lock).end())
				throw glare::Exception("Couldn't find parcel");

			const Parcel* parcel = res->second.ptr();


			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_parcel_owner = logged_in_user && (parcel->owner_id == logged_in_user->id); // If the user is logged in and owns this parcel:


			if(logged_in_user)
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

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
						page += "<div class=\"inline-block\">Screenshot processing...</div>     \n";
					else
						page += "<div class=\"inline-block\"><a href=\"/screenshot/" + toString(screenshot_id) + "\"><img src=\"/screenshot/" + toString(screenshot_id) + "\" width=\"320px\" alt=\"screenshot\" /></a></div>   \n";
				}
			}

			const Vec3d pos = parcel->getVisitPosition();
			
			const std::string weclient_URL = "/webclient?x=" + doubleToStringMaxNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringMaxNDecimalPlaces(pos.y, 1) + "&z=" + doubleToStringMaxNDecimalPlaces(pos.z, 1);
			const std::string sub_URL = "sub://substrata.info/?x=" + doubleToStringMaxNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringMaxNDecimalPlaces(pos.y, 1) + "&z=" + doubleToStringMaxNDecimalPlaces(pos.z, 1);
			
			page += "<p><a href=\"" + weclient_URL + "\">Visit in web browser</a><br/></p>   \n";
			//page += "<p>For the best experience, enter this URL into the location bar in the Substrata client: " + sub_URL + "</p>";
			page += "<p>Visit in Substrata: <a href=\"" + sub_URL + "\">" + sub_URL + "</a><br/>(Click or enter URL into location bar in Substrata client)</p>   \n";


			// Look up owner
			{
				std::string owner_username;
				auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
				if(user_res == world_state.user_id_to_users.end())
					owner_username = "[No user found]";
				else
					owner_username = user_res->second->name;


				page += "<p>Owner: " + web::Escaping::HTMLEscape(owner_username) + "</p>   \n";
			}

			page += "<p>Writers: ";
			// Look up writers
			for(size_t z=0; z<parcel->writer_ids.size(); ++z)
			{
				// Look up user for id
				std::string writer_username;
				auto user_res = world_state.user_id_to_users.find(parcel->writer_ids[z]);
				if(user_res == world_state.user_id_to_users.end())
					writer_username = "[No user found]";
				else
					writer_username = user_res->second->name;

				page += web::Escaping::HTMLEscape(writer_username);

				if(logged_in_user_is_parcel_owner)
					page += " <small><a href=\"/remove_parcel_writer?parcel_id=" + parcel->id.toString() + "&writer_id=" + parcel->writer_ids[z].toString() + "\">[Remove]</a></small>";

				if(z + 1 < parcel->writer_ids.size())
					page += ", ";
			}
			page += "</p>   \n";
			if(logged_in_user_is_parcel_owner)
				page += "<a href=\"/add_parcel_writer?parcel_id=" + parcel->id.toString() + "\">Add writer</a>";



			page += "<p>Description: " + web::Escaping::HTMLEscape(parcel->description) + "</p>   \n";
			//page += "<p>Created: " + parcel->created_time.timeAgoDescription() + "</p>   \n";
			if(logged_in_user_is_parcel_owner)
				page += "<a href=\"/edit_parcel_description?parcel_id=" + parcel->id.toString() + "\">Edit description</a>";

			const Vec3d span = parcel->aabb_max - parcel->aabb_min;
			const double area = span.x * span.y;
			page += "<p>Dimensions: " + doubleToStringMaxNDecimalPlaces(span.x, 1) + " m x " + doubleToStringMaxNDecimalPlaces(span.y, 1) + " m (area: " + 
				doubleToStringNSigFigs(area, 3) + " m<sup>2</sup>), height: " + doubleToStringMaxNDecimalPlaces(parcel->aabb_max.z - parcel->aabb_min.z, 1) + " m.</p>   \n";


			if(parcel->id.value() >= 430 && parcel->id.value() <= 726) // If this is a market parcel:
				page += "<p><b>This is a small parcel in the market region. (e.g. a market stall)</b></p>";

			if(parcel->id.value() >= 1320 && parcel->id.value() <= 1385) // If this is a tower parcel
				page += "<p>This is single level in a tower.</p>";

			const Vec3d centre = (parcel->aabb_max + parcel->aabb_min) * 0.5;
			const double dist_from_orig = centre.getDist(Vec3d(0, 0, 0));

			page += "<p>Location: x: " + toString((int)centre.x) + ", y: " + toString((int)centre.y) + " (" + doubleToStringNSigFigs(dist_from_orig, 2) + " m from the origin), " + 
				doubleToStringMaxNDecimalPlaces(parcel->aabb_min.z, 1) + " m above ground level</p>  \n";


			page += WebServerResponseUtils::getMapEmbedCode(world_state, /*highlighted_parcel_id=*/parcel->id);

			// Show NFT status
			page += "<h2>NFT status</h2>         \n";
			if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
			{
				page += "<p>This parcel has not been minted as an NFT.</p>";

				if(logged_in_user_is_parcel_owner)
					page += "<p><a href=\"/make_parcel_into_nft?parcel_id=" + parcel->id.toString() + "\">Mint as a NFT</a></p>";
			}
			else if(parcel->nft_status == Parcel::NFTStatus_MintingNFT)
				page += "<p>This parcel is being minted as an Ethereum NFT...</p>";
			else if(parcel->nft_status == Parcel::NFTStatus_MintedNFT)
			{
				auto txn_res = world_state.sub_eth_transactions.find(parcel->minting_transaction_id);
				if(txn_res != world_state.sub_eth_transactions.end())
				{
					const SubEthTransaction* trans = txn_res->second.ptr();
					page += "<p>This parcel has been minted as an Ethereum NFT.</p><p><a href=\"https://etherscan.io/tx/0x" + trans->transaction_hash.toHexString() + "\">View minting transaction on Etherscan</a></p>";
				}
				else
				{
					page += "<p>This parcel has been minted as an Ethereum NFT.  (Could not find minting transaction hash)</p>";
				}
				page += "<p><a href=\"https://opensea.io/assets/ethereum/0xa4535f84e8d746462f9774319e75b25bc151ba1d/" + parcel->id.toString() + "\">View on OpenSea</a></p>";
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
				page += "This parcel is not up for auction on this site.";

			/* // Don't show past auctions for now.
			
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
			*/



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

			if(logged_in_user_is_parcel_owner) // If the user is logged in and owns this parcel:
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

				page += "<form action=\"/admin_mark_parcel_as_nft_minted_post\" method=\"post\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
				page += "<input type=\"submit\" value=\"Mark parcel as NFT-minted\" onclick=\"return confirm('Are you sure you want to mark this parcel as NFT-minted?');\" >";
				page += "</form>";
	
				page += "<form action=\"/admin_mark_parcel_as_not_nft_post\" method=\"post\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
				page += "<input type=\"submit\" value=\"Mark parcel as not an NFT\" onclick=\"return confirm('Are you sure you want to mark this parcel as not an NFT?');\" >";
				page += "</form>";

				page += "<form action=\"/admin_retry_parcel_mint_post\" method=\"post\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
				page += "<input type=\"submit\" value=\"Retry parcel minting\" onclick=\"return confirm('Are you sure you want to retry minting?');\" >";
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


void renderEditParcelDescriptionPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	const int parcel_id = request.getURLIntParam("parcel_id");

	std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit parcel description");
	page += "<div class=\"main\">   \n";

	{ // Lock scope

		Lock lock(world_state.mutex);

		// Lookup parcel
		const auto res = world_state.getRootWorldState()->parcels.find(ParcelID(parcel_id));
		if(res != world_state.getRootWorldState()->parcels.end())
		{
			Parcel* parcel = res->second.ptr();

			page += "<form action=\"/edit_parcel_description_post\" method=\"post\" id=\"usrform\">";
			page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + toString(parcel_id) + "\"><br>";
			page += "<textarea rows=\"8\" cols=\"80\" name=\"description\" form=\"usrform\">" + web::Escaping::HTMLEscape(parcel->description) + "</textarea><br>";
			page += "<input type=\"submit\" value=\"Update description\">";
			page += "</form>";
		}
	} // End lock scope

	page += "</div>   \n"; // end main div
	page += WebServerResponseUtils::standardFooter(request, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAddParcelWriterPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	const int parcel_id = request.getURLIntParam("parcel_id");

	std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Add writer to parcel");
	page += "<div class=\"main\">   \n";

	page += "Add a user that will have write permissions for the parcel.  They will be able to create, edit and delete objects in the parcel.";

	{ // Lock scope

		Lock lock(world_state.mutex);

		const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user)
		{
			const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
			if(!msg.empty())
				page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
		}

		// Lookup parcel
		const auto res = world_state.getRootWorldState()->parcels.find(ParcelID(parcel_id));
		if(res != world_state.getRootWorldState()->parcels.end())
		{
			page += "<form action=\"/add_parcel_writer_post\" method=\"post\" id=\"usrform\">";
			page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + toString(parcel_id) + "\"><br>";
			page += "Writer username: <input type=\"text\" name=\"writer_name\" value=\"\"><br>";
			page += "<input type=\"submit\" value=\"Add as writer\">";
			page += "</form>";
		}
	} // End lock scope

	page += "</div>   \n"; // end main div
	page += WebServerResponseUtils::standardFooter(request, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderRemoveParcelWriterPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	const int parcel_id = request.getURLIntParam("parcel_id");
	const int writer_id = request.getURLIntParam("writer_id");

	std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Remove writer from parcel");
	page += "<div class=\"main\">   \n";

	{ // Lock scope

		Lock lock(world_state.mutex);

		const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user)
		{
			const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
			if(!msg.empty())
				page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
		}

		// Lookup writer
		const auto writer_res = world_state.user_id_to_users.find(UserID(writer_id));
		if(writer_res != world_state.user_id_to_users.end())
		{
			page += "Are you sure you want to remove the user " + web::Escaping::HTMLEscape(writer_res->second->name) + " as a writer from the parcel?";

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID(parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				page += "<form action=\"/remove_parcel_writer_post\" method=\"post\" id=\"usrform\">";
				page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + toString(parcel_id) + "\"><br>";
				page += "<input type=\"hidden\" name=\"writer_id\" value=\"" + toString(writer_id) + "\"><br>";
				page += "<input type=\"submit\" value=\"Remove writer\">";
				page += "</form>";
			}
		}
	} // End lock scope

	page += "</div>   \n"; // end main div
	page += WebServerResponseUtils::standardFooter(request, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


// URL for parcel ERC 721 metadata JSON
// See https://docs.opensea.io/docs/metadata-standards
void renderMetadata(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		// Parse parcel id from request path
		Parser parser(request.path);
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
			const double area = span.x * span.y;
			descrip += "Dimensions: " + doubleToStringNSigFigs(span.x, 3) + " m x " + doubleToStringNSigFigs(span.y, 3) + " m (area: " + 
				doubleToStringNSigFigs(area, 3) + " m^2), height: " + doubleToStringNSigFigs(parcel->aabb_max.z, 3) + " m.  ";

			const Vec3d centre = (parcel->aabb_max + parcel->aabb_min) * 0.5;
			const double dist_from_orig = centre.getDist(Vec3d(0, 0, 0));

			descrip += "Location: x: " + toString((int)centre.x) + ", y: " + toString((int)centre.y) + " (" + doubleToStringNSigFigs(dist_from_orig, 2) + " m from the origin).";
			descrip += "  Visit https://substrata.info/parcel/" + toString(parcel_id) + " for more info.";

			page += "\"description\":\"" + web::Escaping::JSONEscape(descrip) + "\","; // "A human readable description of the item. Markdown is supported."

			page += 
				"  \"attributes\": [  			  \n"
				"  {								  \n"
				"    \"trait_type\": \"District\", \n"
				"    \"value\": \"" + parcel->districtName() + "\"		  \n"
				"  }, 								  \n"
				"  {								  \n"
				"    \"display_type\": \"number\",    \n"
				"    \"trait_type\": \"Area (m^2)\",		\n"
				"    \"value\": \"" + doubleToStringNSigFigs(area, 3) + "\"   \n"
				"  }, 								  \n"
				"  {								  \n"
				"    \"display_type\": \"number\",    \n"
				"    \"trait_type\": \"Height (m)\",		\n"
				"    \"value\": \"" + doubleToStringNSigFigs(parcel->aabb_max.z, 3) + "\"   \n"
				"  }, 								  \n"
				"  {								  \n"
				"    \"display_type\": \"number\",    \n"
				"    \"trait_type\": \"Distance from origin (m)\",		\n"
				"    \"value\": \"" + doubleToStringNSigFigs(dist_from_orig, 2) + "\"   \n"
				"  } 								  \n"
				"  ] 								  \n";

			page += "}\n";

		} // end lock scope

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page.c_str(), page.size(), "application/json");
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// See also AdminHandlers::handleRegenerateParcelScreenshots().
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

							// Update pos and angles
							if(shot->cam_pos.z < 100) // If this is the close-in shot.  NOTE: bit of a hack
							{
								parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							}
							else
							{
								parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							}

							shot->is_map_tile = false; // There was a bug where some screenshots got mixed up with map tile screenshots, make sure the screenshot is not marked as a map tile.
							shot->width_px = 650;
							shot->highlight_parcel_id = (int)parcel->id.value();

							shot->state = Screenshot::ScreenshotState_notdone;
							world_state.addScreenshotAsDBDirty(shot);
						}
					}
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenerateParcelScreenshots error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditParcelDescriptionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const web::UnsafeString new_descrip = request.getPostField("description");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && parcel->owner_id == logged_in_user->id) // If the user is logged in and owns this parcel:
				{
					parcel->description = new_descrip.str();
					world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

					world_state.markAsChanged();

					world_state.setUserWebMessage(logged_in_user->id, "Updated description.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditParcelDescriptionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleAddParcelWriterPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const web::UnsafeString writer_name = request.getPostField("writer_name");

		bool added_writer = false;
		std::string message;

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && parcel->owner_id == logged_in_user->id) // If the user is logged in and owns this parcel:
				{
					// Try and find user for writer_name
					User* new_writer_user = NULL;
					for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
						if(it->second->name == writer_name.str())
							new_writer_user = it->second.ptr();

					if(new_writer_user)
					{
						if(!ContainerUtils::contains(parcel->writer_ids, new_writer_user->id))
						{
							added_writer = true;
							parcel->writer_ids.push_back(new_writer_user->id);
							world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);
							message = "Added user as writer.";
						}
						else
						{
							message = "User is already a writer.";
						}
					}
					else
					{
						message = "Could not find a user with that name.";
					}

					if(added_writer)
					{
						world_state.denormaliseData(); // Update parcel writer names
						world_state.markAsChanged();
					}

					world_state.setUserWebMessage(logged_in_user->id, message);
				}
			}
		} // End lock scope

		if(added_writer)
			web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/add_parcel_writer?parcel_id=" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleAddParcelWriterPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRemoveParcelWriterPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const UserID writer_id = UserID(request.getPostIntField("writer_id"));

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && parcel->owner_id == logged_in_user->id) // If the user is logged in and owns this parcel:
				{
					const bool removed = ContainerUtils::removeFirst(parcel->writer_ids, writer_id);

					if(removed)
						world_state.setUserWebMessage(logged_in_user->id, "removed user as writer");
					else
						world_state.setUserWebMessage(logged_in_user->id, "User was not a writer.");

					world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

					world_state.denormaliseData(); // Update parcel writer names
					world_state.markAsChanged();
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRemoveParcelWriterPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}

} // end namespace ParcelHandlers
