/*=====================================================================
MainPageHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MainPageHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "../shared/Version.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>


namespace MainPageHandlers
{


void renderRootPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");
	//const bool logged_in = LoginHandlers::isLoggedInAsNick(data_store, request_info);


	const std::string deployed_version = ::cyberspace_version;

	std::string auction_html;
	{ // lock scope
		Lock lock(world_state.mutex);

		ServerWorldState* root_world = world_state.getRootWorldState().ptr();

		int num_auctions_shown = 0;
		const TimeStamp now = TimeStamp::currentTime();
		auction_html += "<table><tr>\n";
		for(auto it = root_world->parcels.begin(); (it != root_world->parcels.end()) && (num_auctions_shown < 3); ++it)
		{
			Parcel* parcel = it->second.ptr();

			if(!parcel->parcel_auction_ids.empty())
			{
				const uint32 auction_id = parcel->parcel_auction_ids.back(); // Get most recent auction
				auto res = world_state.parcel_auctions.find(auction_id);
				if(res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = res->second.ptr();

					if((auction->auction_state == ParcelAuction::AuctionState_ForSale) && (auction->auction_start_time <= now) && (now <= auction->auction_end_time)) // If auction is valid and running:
					{
						if(!auction->screenshot_ids.empty())
						{
							const uint64 shot_id = auction->screenshot_ids[0]; // Get id of close-in screenshot

							auction_html += "<td><a href=\"/parcel_auction/" + toString(auction_id) + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" width=\"200px\" alt=\"screenshot\" /></a>  <br/>"
								"Current price: " + doubleToString(auction->computeCurrentAuctionPrice()) + "&nbsp;EUR</td>";
						}

						num_auctions_shown++;
					}
				}
			}
		}
		auction_html += "</tr></table>\n";

		if(num_auctions_shown == 0)
			auction_html += "<p>Sorry, there are no parcels for sale right now.  Please check back later!</p>";
	} // end lock scope



	page_out +=
	"	<p>																																																		\n"
	"	Substrata is a multi-user cyberspace/metaverse.  Chat with other users or explore objects and places that other users have created.																		\n"
	"	<br/>																																																	\n"
	"	You can create a free user account and add objects to the world as well!																																\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	Substrata is early in development, please expect rough edges!																																			\n"
	"	</p>																																																	\n"
	"																																																			\n"
	"	<h2>Downloads</h2>																																														\n"
	"	<p>To explore Substrata you will need to install the free client software for your platform:</p>																										\n"
	"	<p>																																																		\n"
	"	Windows - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + "_Setup.exe\">Substrata_v" + deployed_version + "_Setup.exe</a>								\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	OS X - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".pkg\">Substrata_v" + deployed_version + ".pkg</a>												\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	Linux - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".tar.gz\">Substrata_v" + deployed_version + ".tar.gz</a>										\n"
	"	</p>																																																	\n"
	"																																																			\n"
	"	<h2>Buy a land parcel</h2>																																												\n"
	 + auction_html + 
	"   <a href=\"/parcel_auction_list\">View all parcels for sale</a>																																			\n"
	"																																																			\n"
	"	<h2>Community</h2>																																														\n"
	"																																																			\n"
	"	<p>																																																		\n"
	"	<a href=\"https://discord.gg/R6tfYn3\" ><img width=\"200px\" src=\"/files/join_us_on_discord.png\" /></a>																								\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	<a href=\"https://twitter.com/SubstrataVr\" ><img width=\"60px\" src=\"/files/twitter.png\" />@SubstrataVr</a>																							\n"
	"	</p>																																																	\n"
	"																																																			\n"
	"	<h2>CryptoVoxels</h2>																																													\n"
	"	<p>																																																		\n"
	"	We are currently embedding the <a href=\"https://www.cryptovoxels.com\">CryptoVoxels</a> world in Substrata, for testing and fun purposes!																\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	To explore the CryptoVoxels world, just install and run Substrata, and then select from the menu bar:																									\n"
	"<p>																																																		\n"
	"	<b>Go &gt; Go to CryptoVoxels World</b>																																									\n"
	"	</p>																																																	\n"
	"	<h2>Screenshots and Videos</h2>																																											\n"
	"																																																			\n"
	"	<iframe width=\"650\" height=\"400\" src=\"https://www.youtube.com/embed/zAizMS16BvM\" frameborder=\"0\" allow=\"accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe>\n";
	
	page_out += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderTermsOfUse(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");

	page += "<h1>Terms of Service</h1>";

	

	page += "<h2>Overview</h2>";

	page += "These terms of service apply to the Substrata website (at substrata.info) and the Substrata virtual world, which is hosted on the Substrata servers and accessed via the Substrata client software.  "
		"These together constitute the \"Service\"";

	page += "<h2>General conditions</h2>";

	page += "<p>By accessing or using the Service you agree to be bound by these Terms. If you disagree with any part of the terms then you may not access the Service.</p>";

	page += "<p>We reserve the right to refuse service to any one at any time, for any reason.</p>";

	page += "<h2>Changes to terms of service</h2>";

	page += "We reserve the right to change the terms of service.";

	page += "<h2>Parcel ownership</h2>";

	page += "<p>'Not-safe-for-work' parcel content is not currently allowed.  This includes sexual content and violence.</p>";

	page += "<p>Content that is illegal in Germany, New Zealand, or the USA is not allowed</p>";

	page += "<p>Parcel content must not severely and adversely affect the performance or functioning of the Substrata server(s) or client.  (For example, do not upload models with excessive polygon counts or texture resolution)</p>";

	page += "<p>Do not deliberately attempt to crash or degrade the functioning of the server or other users' clients.</p>";

	page += "<h2>Governing law</h2>";

	page += "These Terms of Service shall be governed by and construed in accordance with the laws of New Zealand.";


	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderNotFoundPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, "Substrata");

	//---------- Right column -------------
	page_out += "<div class=\"right\">"; // right div


	//-------------- Render posts ------------------
	page_out += "Sorry, the item you were looking for does not exist at that URL.";

	page_out += "</div>"; // end right div
	page_out += WebServerResponseUtils::standardFooter(request_info, true);
	page_out += "</div>"; // main div
	page_out += "</body></html>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


}  // end namespace MainPageHandlers
