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
		auction_html += "<table style=\"width: 100%;\"><tr>\n";
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

							const double cur_price_EUR = auction->computeCurrentAuctionPrice();
							const double cur_price_BTC = cur_price_EUR * world_state.BTC_per_EUR;
							const double cur_price_ETH = cur_price_EUR * world_state.ETH_per_EUR;

							auction_html += "<td style=\"vertical-align:top\"><a href=\"/parcel_auction/" + toString(auction_id) + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" width=\"200px\" alt=\"screenshot\" /></a>  <br/>"
								+ doubleToString(auction->computeCurrentAuctionPrice()) + "&nbsp;EUR / " + doubleToStringNSigFigs(cur_price_BTC, 2) + "&nbsp;BTC / " + doubleToStringNSigFigs(cur_price_ETH, 2) + "&nbsp;ETH</td>";
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
	"	<h2>Scripting</h2>																																														\n"
	"	<p>Read about <a href=\"/about_scripting\">object scripting in Substrata</a>.</p>																														\n"
	"																																																			\n"
	"	<h2>CryptoVoxels</h2>																																													\n"
	"	<p>																																																		\n"
	"	We are currently embedding the <a href=\"https://www.cryptovoxels.com\">CryptoVoxels</a> world in Substrata, for testing and fun purposes!																\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	To explore the CryptoVoxels world, just install and run Substrata, and then select from the menu bar:																									\n"
	"	<p>																																																		\n"
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


void renderAboutParcelSales(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Parcel sales in Substrata");

	page += "<h2>Dutch Auctions</h2>";

	page += "<p>Parcel sales in Substrata are currently done with a <a href=\"https://en.wikipedia.org/wiki/Dutch_auction\">Dutch (reverse) auction</a>.  A Dutch auction starts from a high price, with the price decreasing over time.</p>";

	page += "<p>The auction stops as soon as someone buys the parcel.  If no one buys the parcel before it reaches the low/reserve price, then the auction stops without a sale.</p>";

	page += "<p>The reason for using a reverse auction is that it avoids the problem of people faking bids, e.g. promising to pay, and then not paying.</p>";
		
	page += "<h2>Payments and Currencies</h2>";

	page += "<h3>PayPal</h3>";

	page += "<p>We accept credit-card payments of normal ('fiat') money, via <a href=\"https://www.paypal.com/\">PayPal</a>.  This option is perfect for people without cryptocurrency or who don't want to use cryptocurrency.</p>";

	page += "<p>Prices on substrata.info are shown in Euros (EUR), but you can pay with your local currency (e.g. USD).  PayPal will convert the payment amount from EUR to your local currency and show it on the PayPal payment page.</p>";

	page += "<h3>Coinbase</h3>";

	page += "<p>We also accept cryptocurrencies via <a href=\"https://www.coinbase.com/\">Coinbase</a>.  We accept all cryptocurrencies that Coinbase accepts, which includes Bitcoin, Ethereum and others.</p>";

	page += "<p>Pricing of BTC and ETH shown on substrata.info is based on the current EUR-BTC and EUR-ETH exchange rate, as retrieved from Coinbase every 30 seconds.</p>";

	page += "<p>The actual amount of BTC and ETH required to purchase a parcel might differ slightly from the amount shown on substrata.info, due to rounding the amount displayed and exchange-rate fluctuations</p>";

	page += "<h2>Building on your recently purchased Parcel</h2>";

	page += "<p>Did you just win a parcel auction? congratulations!  Please restart your Substrata client, so that ownership changes of your Parcel are picked up.</p>";

	page += "<p>To view your parcel, click the 'Show parcels' toolbar button in the Substrata client, then double-click on your parcel.  The parcel should show you as the owner in the object editor.";
	page += "If the owner still says 'MrAdmin', then the ownership change has not gone through yet.</p>";

	page += "<br/><br/>";
	page += "<a href=\"/\">&lt; Home</a>";

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAboutScripting(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Scripting in Substrata");
	
	page += "<p>Scripting in the Substrata metaverse is currently done with the <a href=\"https://github.com/glaretechnologies/winter\">Winter programming language</a>.</p>";

	page += "<p>Winter is a high-performance functional programming language, made by us at Glare Technologies.  We use it in our other software "
		"<a href=\"https://www.indigorenderer.com/\">Indigo Renderer</a> and <a href=\"https://www.chaoticafractals.com/\">Chaotica</a>.</p>";

	page += "<h3>Winter Language reference</h3>";

	page += "<p>See the <a href=\"https://github.com/glaretechnologies/winter\">Github Winter page</a> for the language reference documentation.</p>";

	page += "<h3>Client-side execution</h3>";

	page += "<p>Scripts in Substrata are executed in the Substrata client program (e.g. they are executed 'client-side').  Winter programs are restricted in what they can do, so are safe to execute client-side.  "
		"(Although we can't rule out all bugs in the Winter execution environment)</p>";

	page += "<h3>Scripting an object</h3>";

	page += "<p>To make a script for an object, you edit code in the 'Script' text edit box in the object editor in the Substrata client, after selecting an object.   You can only edit scripts on objects that you own (e.g. that you created).</p>";

	page += "<h3>Scriptable functions</h3>";

	page += "<p>To script the behaviour of an object, you can define either of two functions:</p>";

	page += "<h4>evalRotation</h4>";

	page += "<code>def evalRotation(float time, WinterEnv env) vec3</code>";

	page += "<p>time is the current global time in the Substrata metaverse.</p>";

	page += "<p>This function returns a 3-vector, where the direction of the vector defines the axis of rotation, and the length of the vector defines the counter-clockwise rotation around the axis, in radians.</p>";

	page += "<p>For example, the rotating wind turbine blades use the following script:<p>";

	page += "<code>def evalRotation(float time, WinterEnv env) vec3 : vec3(-0.6, 0.0, 0.0) * time</code>";

	page += "<p>This rotates the blades clockwise (due to the minus sign) around the x axis at a constant rate.</p>";

	page += "<h4>evalTranslation</h4>";

	page += "<code>def evalTranslation(float time, WinterEnv env) vec3</code>";

	page += "<p>This function returns a 3-vector, which defines a spatial translation from the usual position of an object (as placed by a user or entered in the object editor).  The translation can be a function of time "
		" to allow object movement.</p>";

	page += "<p>For example, this script makes an object move back and forth along the x axis:</p>";

	page += "<code>def evalTranslation(float time, WinterEnv env) vec3 : vec3(sin(time * 1.51) * 0.1, 0, 0)</code>";

	page += "<h2>Future Scripting</h2>";

	page += "<p>We may allow server-side scripting in the future, using a language like Javascript, and with some way of maintaining state.</p>";

	page += "<p>We plan to allow users to run their own server as well, to control their parcels, which will allow arbitrarily complicated code to affect their Substrata parcels.</p>";

	page += "<br/><br/>";
	page += "<a href=\"/\">&lt; Home</a>";

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
