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
#include "LoginHandlers.h"
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
	//std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");
	//const bool logged_in = LoginHandlers::isLoggedInAsNick(data_store, request_info);

	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, /*page title=*/"Substrata");
	page_out +=
		"	<body style=\"margin-top: 46px;\">\n"
		"	<div id=\"login\" style=\"float: right; margin-top: -8px;\">\n"; // Start login div

	web::UnsafeString logged_in_username;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username);

	if(logged_in)
	{
		page_out += "You are logged in as <a href=\"/account\">" + logged_in_username.HTMLEscaped() + "</a>";

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
		"	</div>																									\n"; // End login div


	page_out += "<img src=\"/files/logo_main_page.png\" alt=\"substrata logo\" style=\"padding-bottom:20px\" />";


	const std::string deployed_version = "0.71";// ::cyberspace_version;

	std::string auction_html;
	{ // lock scope
		Lock lock(world_state.mutex);

		ServerWorldState* root_world = world_state.getRootWorldState().ptr();

		int num_auctions_shown = 0; // Num substrata auctions shown
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

					if(auction->currentlyForSale(now)) // If auction is valid and running:
					{
						if(!auction->screenshot_ids.empty())
						{
							const uint64 shot_id = auction->screenshot_ids[0]; // Get id of close-in screenshot

							const double cur_price_EUR = auction->computeCurrentAuctionPrice();
							const double cur_price_BTC = cur_price_EUR * world_state.BTC_per_EUR;
							const double cur_price_ETH = cur_price_EUR * world_state.ETH_per_EUR;

							auction_html += "<td style=\"vertical-align:top\"><a href=\"/parcel_auction/" + toString(auction_id) + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" width=\"200px\" alt=\"screenshot\" /></a>  <br/>"
								"&euro;" + doubleToStringNDecimalPlaces(cur_price_EUR, 2) + " / " + doubleToStringNSigFigs(cur_price_BTC, 2) + "&nbsp;BTC / " + doubleToStringNSigFigs(cur_price_ETH, 2) + "&nbsp;ETH</td>";
						}

						num_auctions_shown++;
					}
				}
			}
		}
		auction_html += "</tr></table>\n";

		// If no auctions on substrata site were shown, show OpenSea auctions, if any.
		int opensea_num_shown = 0;
		if(num_auctions_shown == 0)
		{
			auction_html += "<table style=\"width: 100%;\"><tr>\n";
			for(auto it = world_state.opensea_parcel_listings.begin(); (it != world_state.opensea_parcel_listings.end()) && (opensea_num_shown < 3); ++it)
			{
				const OpenSeaParcelListing& listing = *it;

				auto parcel_res = root_world->parcels.find(listing.parcel_id); // Look up parcel
				if(parcel_res != root_world->parcels.end())
				{
					const Parcel* parcel = parcel_res->second.ptr();

					if(parcel->screenshot_ids.size() >= 1)
					{
						const uint64 shot_id = parcel->screenshot_ids[0]; // Close-in screenshot

						const std::string opensea_url = "https://opensea.io/assets/0xa4535f84e8d746462f9774319e75b25bc151ba1d/" + listing.parcel_id.toString();

						auction_html += "<td style=\"vertical-align:top\"><a href=\"/parcel/" + parcel->id.toString() + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" width=\"200px\" alt=\"screenshot\" /></a>  <br/>"
							"<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a> <a href=\"" + opensea_url + "\">View&nbsp;on&nbsp;OpenSea</a></td>";
					}

					opensea_num_shown++;
				}
			}
			auction_html += "</tr></table>\n";
		}

		if(num_auctions_shown == 0 && opensea_num_shown == 0)
			auction_html += "<p>Sorry, there are no parcels for sale here right now.  Please check back later!</p>";

	} // end lock scope



	page_out +=
	"	<p>																																																		\n"
	"	Substrata is a multi-user cyberspace/metaverse.  Chat with other users or explore objects and places that other users have created.																		\n"
	"	<br/>																																																	\n"
	"	You can create a free user account and add objects to the world as well!																																\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	Land parcels in Substrata are <a href=\"/parcel_auction_list\">for sale</a>, and can be minted as Ethereum NFTs.																																\n"
	"	</p>																																																\n"
	"	<p>																																																		\n"
	"	Substrata is early in development, please expect rough edges!																																			\n"
	"	</p>																																														\n"
	"	<p><a href=\"/about_substrata\">Read about our goals and plans for Substrata</a></p>				\n"
	"	<p><a href=\"/faq\">Read general question and answers about Substrata</a></p>				\n"
	"																																																			\n"
	"	<h2>Downloads</h2>																																														\n"
	"	<p>To explore Substrata you will need to install the free client software for your platform:</p>																										\n"
	"	<p>																																																		\n"
	"	Windows - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + "_Setup.exe\">Substrata_v" + deployed_version + "_Setup.exe</a>								\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	MacOS - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".pkg\">Substrata_v" + deployed_version + ".pkg</a>												\n"
	"	</p>																																																	\n"
	"	<p>																																																		\n"
	"	Linux - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".tar.gz\">Substrata_v" + deployed_version + ".tar.gz</a>										\n"
	"	</p>																																																	\n"
	"																																																			\n"
	"	<h2>Buy a land parcel</h2>																																												\n"
	 + auction_html + 
	"   <br/> <a href=\"/parcel_auction_list\">View all parcels for sale</a>																																			\n"
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
	"	<h2>Screenshots and Videos</h2>																																											\n"
	"																																																			\n"
	"	<iframe width=\"650\" height=\"400\" src=\"https://www.youtube.com/embed/5cKDiktip2w\" frameborder=\"0\" allow=\"accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe>\n"
	"	<iframe width=\"650\" height=\"400\" src=\"https://www.youtube.com/embed/CcWYmJLdnFI\" frameborder=\"0\" allow=\"accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe>\n"
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
		;
	
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

	page += "<p>We accept credit card payments of normal (&lsquo;fiat&rsquo;) money, via <a href=\"https://www.paypal.com/\">PayPal</a>.  This option is perfect for people without cryptocurrency or who don't want to use cryptocurrency.</p>";

	page += "<p>Prices on substrata.info are shown in Euros (EUR / &euro;), but you can pay with your local currency (e.g. USD).  PayPal will convert the payment amount from EUR to your local currency and show it on the PayPal payment page.</p>";

	page += "<h3>Coinbase</h3>";

	page += "<p>We also accept cryptocurrencies via <a href=\"https://www.coinbase.com/\">Coinbase</a>.  We accept all cryptocurrencies that Coinbase accepts, which includes Bitcoin, Ethereum and others.</p>";

	page += "<p>Pricing of BTC and ETH shown on substrata.info is based on the current EUR-BTC and EUR-ETH exchange rate, as retrieved from Coinbase every 30 seconds.</p>";

	page += "<p>The actual amount of BTC and ETH required to purchase a parcel might differ slightly from the amount shown on substrata.info, due to rounding the amount displayed and exchange-rate fluctuations.</p>";

	page += "<h2>Building on your recently purchased Parcel</h2>";

	page += "<p>Did you just win a parcel auction? Congratulations!  Please restart your Substrata client, so that ownership changes of your Parcel are picked up.</p>";

	page += "<p>To view your parcel, click the 'Show parcels' toolbar button in the Substrata client, then double-click on your parcel.  The parcel should show you as the owner in the object editor.";
	page += " If the owner still says 'MrAdmin', then the ownership change has not gone through yet.</p>";

	page += "<h2>Reselling Parcels and NFTs</h2>";

	page += "<p>You can mint a substrata parcel you own as an Ethereum NFT.  This will allow you to sell it or otherwise transfer it to another person.</p>";

	page += "<p>See the <a href=\"/faq\">FAQ</a> for more details.</p>";


	page += "<br/><br/>";
	page += "<a href=\"/\">&lt; Home</a>";

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderFAQ(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"General questions about Substrata");

	page += 
		
"	<p><b>What is																										"
"	Substrata? Where can I find more information about this project?</b></p>											"
"	<p>Substrata is a free, online 3D																					"
"	metaverse, where users can explore, chat, play and build. 															"
"	</p>																												"
"	<p>																													"
"	You can find more information at this page on our website:															"
"	<a href=\"https://substrata.info/about_substrata\">about Substrata</a>							"
"	</p>																												"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<p><b>Is there a																									"
"	roadmap?</b></p>																									"
"																												"
"	<p>																													"
"	There is not a formal roadmap, but you can read about some of the													"
"	goals for the project on this page:																					"
"	<a href=\"https://substrata.info/about_substrata\">about Substrata</a>							"
"	</p>																												"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<p><b>Who is behind																									"
"	Substrata?</b></p>																									"
"																												"
"	<p><span>The																										"
"	company behind Substrata is <a href=\"https://www.glaretechnologies.com/\">Glare										"
"	Technologies Limited</a>. We are also the developers of </span>														"
"	<a href=\"https://www.indigorenderer.com/\">Indigo Renderer</a> and													"
"	<a href=\"https://www.chaoticafractals.com/\">Chaotica Fractals</a>. 													"
"	</p>																												"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<p>																													"
"	<b>You say Substrata is a &lsquo;metaverse&rsquo;.</b> <b>What does													"
"	that mean?</b></p>																									"
"	<p>																													"
"	We are aiming for Substrata to become one of the most important, and												"
"	maybe the dominant 3D metaverse on the internet &ndash; just like the												"
"	Web is the dominant '2D metaverse' today. 																			"
"	</p>																												"
"	<p><span>The																										"
"	main</span> world on substrata.info is where new users will join the												"
"	world. The centre of this world will be like a town square or forum. 												"
"	</p>																												"
"	<p>Each user also has their own personal world, which anyone can													"
"	visit, at sub://substrata.info/username in the Substrata client														"
"	software. Users can upload and build whatever they want in there													"
"	(with some disk usage limits etc..)</p>																				"
"	<p>In future you will																								"
"	be able to run some server software on your own server, to serve data												"
"	for your substrata.info parcel. 																					"
"	</p>																												"
"	<p><span>Therefore																									"
"	the &lsquo;metaverse&rsquo; label refers to this </span><span>future												"
"	roadmap to have a series of interlinked worlds contained within														"
"	Substrata, with users having the option of running their own														"
"	Substrata servers.</span></p>																						"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>What is the link																								"
"	between Substrata and CryptoVoxels?</b></p>																			"
"	<p>We are currently embedding a snapshot																			"
"	of the <a href=\"https://www.cryptovoxels.com/\">CryptoVoxels</a> world												"
"	in Substrata, for testing and fun purposes!<b> </b>																	"
"	</p>																												"
"	<p>To explore the CryptoVoxels world, just install and run Substrata,												"
"	and then select from the menu bar: <span>Go																			"
"	&gt; Go to CryptoVoxels World</span> 																				"
"	</p>																												"
"	<p>																													"
"	Note, this is a snapshot of the CryptoVoxels world and is likely to													"
"	be out of date.</p>																									"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"

"	<h1>Buying, selling and sharing ownership of														"
"	Substrata parcels</h1>																								"
"	<p><b>How can I buy a																								"
"	Substrata parcel?</b></p>																							"
																											
"	<p>																													"
"	Parcels will be regularly available for sale on the Substrata website												"
"	and on OpenSea. The best way to learn about upcoming sales is to													"
"	follow Substrata on Twitter and the parcel sale announcement channel												"
"	on Discord.</p>																										"
																											
"	<p>																													"
"	If there are no auctions running at the moment, you could also try													"
"	and purchase a parcel in the secondary market on OpenSea.</p>														"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<p><b>Is Substrata																									"
"	built on the blockchain?</b></p>																					"
																											
"	<p>																													"
"	By default, parcels in Substrata are not NFTs. However, once you own												"
"	a parcel you can request that your parcel be minted as an Ethereum													"
"	NFT.</p>																											"
																											
"	<p>																													"
"	In addition, we sometimes mint the parcels as NFTs from the beginning												"
"	and then auction them on OpenSea.</p>																				"
																											
"	<p>																													"
"	Therefore, parcels for sale on the Substrata website will not be NFTs												"
"	(but can be made into NFTs on request), whereas parcels for sale on													"
"	OpenSea will already be NFTs.</p>																					"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<p><b>How to mint a																									"
"	parcel you own as an NFT</b></p>																					"
																											
"	<p>																													"
"	1) Log into your existing account at the Substrata website:															"
"	<a href=\"https://substrata.info/account\">account</a></p>										"
"	<p>																													"
"	2) In the parcels section you should see the parcels you own listed.</p>											"
"	<p>																													"
"	3) With Metamask installed in your web browser, link your Substrata													"
"	account to your eth address. You do this by signing a message with													"
"	Metamask.</p>																										"
"																										"
"	<p>																													"
"	4) Navigate to the parcel you want to create as an NFT in the account												"
"	section. Click that you want to mint the parcel as an NFT. You do not												"
"	need to pay the eth gas fee for minting the NFT &ndash; that will be												"
"	covered by Substrata.</p>																							"
"																												"
"	<p>																													"
"	5) The request to mint the NFT will be added to the Substrata queue.												"
"	You&rsquo;ll need to wait for the NFT to be minted by Nick. Note,													"
"	this is still a semi-manual process so there may be a delay.</p>													"
"																												"
"	<p>																													"
"	6) Once the minting process is complete, the eth address linked to													"
"	your substrata account will be assigned as the owner of the Substrata												"
"	parcel. The parcel should appear on the OpenSea account for your eth												"
"	address.</p>																										"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>How do I claim																								"
"	ownership of an NFT I bought in Substata itself? </b>																"
"	</p>																												"
"																												"
"	<p>																													"
"	1) Log in to your account (or create one) at the Substrata website:													"
"	<a href=\"https://substrata.info/account\">account</a></p>										"
"	<p>																													"
"	2) If you haven&rsquo;t already, link your Substrata account to the													"
"	eth address you control which owns the Substrata NFT. To do this													"
"	you&rsquo;ll need Metamask installed in your web browser, and to then												"
"	sign a message to prove you own the eth address.</p>																"
"	<p>																													"
"	3) Go to <a href=\"https://substrata.info/prove_parcel_owner_by_nft\">prove parcel owner by nft</a>					"
"	The website should display your linked eth address if you have														"
"	successfully linked your Substrata account to your eth address.</p>													"
"	<p>												"
"	4) Type in the parcel number you own as an NFT (e.g. 151) and click													"
"	the claim parcel button. Wait a few seconds for the process to work.												"
"	Once the process is complete, the parcel should be listed on the													"
"	account page.</p>																									"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"

"	<p><b>How do I update																								"
"	the screenshots for my parcel on the Substrata website/Opensea?</b></p>												"
"	<p>																													"
"	1) Log into your existing account at the Substrata website:															"
"	<a href=\"https://substrata.info/account\">account</a></p>										"
"	<p>																													"
"	2) In the parcels section you should see the parcels you own listed.												"
"	Click on the parcel for which you want the screenshot to be updated.</p>											"
"	<p>																													"
"	3) Near the bottom of the page you will see &lsquo;Parcel owner														"
"	tools&rsquo;. Click &lsquo;regenerate screenshots&rsquo;.</p>														"
"	<p>																													"
"	4) Wait a few minutes. The images should then update on both the													"
"	Substrata website and OpenSea.</p>																					"
"	<p>																													"
"	Note: the screenshotting process is done by the Screenshot bot. If													"
"	the process isn&rsquo;t working you can check the status of the bot:												"
"	<a href=\"https://substrata.info/bot_status\">bot status</a>									"
"	</p>																												"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>How do I update																								"
"	the description of my parcel on the Substrata website?</b></p>														"
"	<p>																													"
"	1) Log into your existing account at the Substrata website:															"
"	<a href=\"https://substrata.info/account\">account</a></p>										"
"	<p>												"
"	2) In the parcels section you should see the parcels you own listed.												"
"	Click on the parcel for which you want the description to be updated.</p>											"
"	<p>																													"
"	3) Click &lsquo;Edit description&rsquo; and input your changes.</p>													"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>Is it possible to																								"
"	share ownership of parcels in Substrata?</b></p>																	"
"	<p><span>Yes,																										"
"	it is possible to add other users as &lsquo;writers&rsquo; of your													"
"	parcel. </span><span>This means the user																			"
"	</span>will be able to create, edit and delete objects in your														"
"	parcel. 																											"
"	</p>																												"
"	<p>																													"
"	1) Log into your existing account at the Substrata website:															"
"	<a href=\"https://substrata.info/account\">account</a></p>										"
"	<p>																													"
"	2) On your user page, navigate to the parcel you want to share.</p>													"
"	<p>																													"
"	3) Click &lsquo;add writer&rsquo; and input the Substrata username of												"
"	the user you want to add as a writer for your parcel.</p>															"
"	<p>																													"
"	Another option is you can make your parcel temporarily or permanently												"
"	&lsquo;All Writeable&rsquo; so that any user can add, edit and delete												"
"	objects in your parcel. To do this, select your parcel in world and													"
"	in the object editor check the &lsquo;All Writeable&rsquo; box.</p>													"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<h1>Building and creating in Substrata</h1>															"
"	<p><b>How do I create																								"
"	objects in Substrata?</b></p>																						"
"	<p>																													"
"	To create an object in the main world you need to either be in a													"
"	parcel you own or the sandbox (parcel #20). Alternatively you could													"
"	create objects in your personal world where there are no															"
"	restrictions.</p>																									"
"	<p>																													"
"	There are two ways to create models. In the client you can either													"
"	select &lsquo;Add Model / Image / Video&rsquo; and follow the														"
"	dialogue prompts. Or you can create a model inside Substrata using													"
"	voxels by selecting &lsquo;Add Voxels&rsquo; (see the following														"
"	question for more information).</p>																					"
"	<p>																													"
"	<span>Supported model formats are:													"
"	OBJ, GLTF, GLB, VOX, STL and IGMESH</span></p>																		"
"	<p>																													"
"	<span>Supported image formats are:													"
"	JPG, TGA, BMP, PNG, TIF, EXR, GIF, KTX, KTX2</span></p>																"
"	<p>																													"
"	<span>Supported video formats are:													"
"	MP4</span></p>																										"
"	<p>																													"
"	<span>Note: please do not add models												"
"	with a very large number of polygons, or with large file sizes (including												"
"	textures), as it may cause performance issues for other users.</span></p>											"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>How do voxels																									"
"	work in Substrata?</b></p>																							"
"	<p>																													"
"	<span>You can either add a voxel													"
"	created outside Substrata (i.e. add a 3d model in one of the														"
"	supported formats), or create a voxel inside Substrata itself.</span></p>											"
"	<p>																													"
"	<span>To create a voxel in Subsrata													"
"	move to a parcel where you have edit privileges and click &lsquo;Add												"
"	Voxels&rsquo; &ndash; this creates the first block (a grey cube). A													"
"	help box should pop up with extra information on how to edit voxels.</span></p>										"
"	<p>																													"
"	<span>The main thing to know is that												"
"	once you&rsquo;ve selected the voxel you can &lsquo;Ctrl +															"
"	left-click&rsquo; to add a voxel on the surface of the cube you click												"
"	on. When you hold down Ctrl and hover over a voxel you will see a													"
"	preview of where the new voxel will appear when you click.</span></p>												"
"	<p>																													"
"	<span>Note voxels do not have to													"
"	remain a perfect cube. You can amend the dimensions of the voxel by													"
"	tweaking the &lsquo;Scale&rsquo; of the object in the Editor box.</span></p>										"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>How do I animate																								"
"	objects in Substrata?</b></p>																						"
"	<p><span>Scripting in the Substrata																					"
"	metaverse is currently done with the <a href=\"https://github.com/glaretechnologies/winter\">Winter					"
"	programming language</a>. You can read more on the Substrata website												"
"	<a href=\"/about_scripting\">about scripting</a>							"
"	</span>																												"
"	</p>																												"
"	<p><span>In																											"
"	short, t</span>o make a script for an object, you edit code in the													"
"	'Script' text edit box in the object editor in the Substrata client,												"
"	after selecting an object. You can only edit scripts on objects that												"
"	you own (e.g. that you created).</p>																				"
"	<p>																													"
"	<br/>																												"
"																														"
"	</p>																												"
"	<p><b>Does Substrata																								"
"	have any policies on what stuff I can put in my parcel?</b></p>														"
"	<p>Yes. For example																									"
"	'Not-safe-for-work' parcel content is not currently allowed. This													"
"	includes sexual content and violence. 																				"
"	</p>																												"
"	<p>For more																											"
"	information, please read the Terms of Service on our website:														"
"	<a href=\"/terms\">terms</a>												"
"	</p>																												"
"	<p><br/>																											"
"																														"
"	</p>																												"
"	<h1>Troubleshooting</h1>																			"
"	<p><b>I'm having																								"
"	problems with Substrata &ndash; where can I get help?</b></p>														"
"	<p>																													"
"																														"
"																														"
"																														"
"	The best place to get support is on the Substrata discord channel:													"
"	<a href=\"https://discord.com/invite/R6tfYn3\">https://discord.com/invite/R6tfYn3</a>									"
"	</p>																													"
;

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


void renderAboutSubstrataPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Our vision for Substrata");

	page += "<p>Substrata is a free, online 3D metaverse, where users can explore, chat, play and build.</p>";

	page += "<a href=\"/files/substrata_24th_may_2021.jpg\"><img src=\"/files/substrata_24th_may_2021_small.jpg\" /></a>";
	page += "<div class=\"caption\">Substrata as of 24th May 2021</div>";

	page += " We are aiming for Substrata to become one of the most important, and maybe the dominant 3D metaverse on the internet - "
		" just like the Web is the dominant '2D metaverse' today.";

	page += " Read on for how we plan to achieve this!";

	page += "<h2>Beautiful and realistic graphics</h2>";

	page += "We are using the code and knowledge from the development of <a href=\"https://www.indigorenderer.com/\">Indigo Renderer</a>, a photorealistic ray tracer, "
		" <a href=\"https://www.chaoticafractals.com/\">Chaotica Fractals</a>, "
		"and more than 10 years of work in the high-end graphics area, in the development of Substrata."
		" For example, we use Indigo to bake lightmaps for Substrata, which allows highly accurate lighting with global illumination, while still running on normal computers.";

	page += "<a href=\"/files/indigo_ref2.jpg\"><img src=\"/files/indigo_ref2_small.jpg\" /></a>";
	page += "<div class=\"caption\">A gallery in Substrata visualised in Indigo.  Art by <a href=\"https://codyellingham.com/\">Cody Ellingham</a></div>";

	page += "<a href=\"/files/lightmaps.jpg\"><img src=\"/files/lightmaps_small.jpg\" /></a>";
	page += "<div class=\"caption\">Lighting information from Indigo ready to be baked into lightmaps</div>";


	page += "Our 3D engine also focuses on handling the large amounts of dynamic content needed for a shared metaverse."
		" We tested loading the entire of the <a href=\"https://www.cryptovoxels.com/\">Cryptovoxels</a> world into the engine and rendering it in realtime to test the engine scalability:";

	page += "<a href=\"/files/CV_world.jpg\"><img src=\"/files/CV_world_small.jpg\" /></a>";
	page += "<div class=\"caption\">The entire Cryptovoxels world (as of March 2021) rendered in Substrata</div>";

	page += "<h2>A main shared world</h2>";

	page += "The main world on substrata.info is where new users will join the world.  The centre of this world will be like a "
		" town square or forum.";

	page += "<h2>Free-form content creation by users</h2>";

	page += "<p>Our philosophy is that users should be free to create whatever they want on their parcels or in their personal worlds, with as few limits as possible.";
	page += " Technical limits such as 3D model and texture resolution are things we will be trying to reduce as much as possible by improving our 3D engine and trying to make it as scalable as possible.";
	page += " We expect this to be a major area of ongoing development - basically trying to make the engine keep up with what users are throwing at it.</p>";

	page += "<a href=\"/files/illuvio.jpg\"><img src=\"/files/illuvio_small.jpg\" /></a>";
	page += "<div class=\"caption\">Illuvio's strange Susbtrata parcel</a></div>";

	page += "<p>We expect to allow not-safe-for-work (NSFW, e.g. adult) content, but hidden behind a show-NSFW option that will be off by default.";
	page += " We will of course not allow illegal content, see our <a href=\"/terms\">terms of service</a> for more detail on that matter.</p>";

	page += "<h2>Immersive Audio</h2>";

	page += "Audio is an important aspect of immersion in a virtual world, together with graphics.  We already have spatial audio up and running:";

	page += "<iframe src=\"https://player.vimeo.com/video/554206954?badge=0&amp;autopause=0&amp;player_id=0&amp;app_id=58479\" width=\"650\" height=\"366\" frameborder=\"0\" allow=\"autoplay; fullscreen; picture-in-picture\" "
		" allowfullscreen title=\"Spatial audio footsteps\"></iframe>";
	page += "<div class=\"caption\">Spatial audio footsteps - listen with headphones!</div>";

	page += "Users will be able to upload their own sounds, or even procedurally generate sounds:";

	page += "<iframe src=\"https://player.vimeo.com/video/554207024?badge=0&amp;autopause=0&amp;player_id=0&amp;app_id=58479\" width=\"650\" height=\"366\" frameborder=\"0\" allow=\"autoplay; fullscreen; picture-in-picture\" "
		"allowfullscreen title=\"Spatial audio cubes\"></iframe>";
	page += "<div class=\"caption\">Spatial audio with procedurally generated audio sources - listen with headphones!</div>";

	page += "<h2>Land Sales</h2>";
	page += " Users can purchase land parcels in this main shared world - "
		"<a href=\"/parcel_auction_list\">see the parcels currently for sale</a>.  In general, the closer"
		" the parcel is to the centre of the world, the more expensive it will be, as it will get more exposure to foot traffic.  "
		"Think owning a property on Times Square in New York.";

	page += " We sell land on Substrata (as opposed to just giving it away for free) for various reasons:";
	page += "<ul><li>Revenue from sales helps fund code development and server costs</li>";
	page += "<li>Steady land sales results in a manageable influx of new builders (users who can edit objects in their parcels), allowing us to fix bugs and tweak features steadily"
		" without having to deal with thousands of simultaneous new builders</li>";
	page += "<li>Owning land incentivises users to improve it by building cool stuff on it (in theory at least!).</li>";
	page += "</ul>";

	page += "We want to support a secondary market in land sales, see <a href=\"/about_parcel_sales\">about parcel sales</a> for more info.";

	page += "<h2>Personal worlds</h2>";
	page += "<p>Each user also has their own personal world, which anyone can visit, at sub://substrata.info/username in the Substrata client software. "
		"Users can upload and build whatever they want in there (with some disk usage limits etc..)";

	
	page += "<h2>Running a server to serve your parcel</h2>";

	page += "Imagine if the entire Web ran on Google's computers - that wouldn't be very democratic and decentralised.";
	page += " Instead, today anyone can run a webserver, and serve a website from it, if you have the right software installed on it.";

	page += " In a similar vein, you will be able to run some server software on your own server, to serve data for your substrata.info parcel.";
	page += " This has a few advantages:";
	page += "<ul><li>No file size or disk usage restrictions - you can serve 3d models, textures, and movies with as high resolution as you wish.";
	page += " Just bear in mind that user clients may display lower resolutions of your assets.";

	page += "</li><li>Full control over objects in your parcel - If you are a programmer, you can script / program the objects in your parcel to do"
		" anything you wish.  Since the code is running on your server, there are no sandboxing issues or restrictions on what the code can do.";
	page += "</li></ul>";
	page += "<h2>Running a server to serve your own world</h2>";

	page += "You will also be able to run the substrata server software on your server, to run an entire world, with parcels, users etc..";
	page += " We intend for substrata.info to remain the main world however!";


	page += "<h2>Open Source</h2>";

	page += "We plan to open-source Substrata at some point, probably once the world on substrata.info has reached critical mass.";
	page += " I think open source is essential for Substrata to really become widely used and the bedrock of the metaverse.";

	page += "<h2>Open Protocol</h2>";
	page += "We will publish documentation on the Substrata network protocol, and all other documentation required for interoperability,"
		" such as the 3d mesh format specification.  This will allow other programmers to implement Substrata clients, servers, bots, "
		"agents etc..";

	page += "<h2>Other goals</h2>";

	page += "<h3>Voice chat with other users with spatial audio</h3>"
		"You will be able to chat using a headset with other users nearby in virtual space to you.";

	page += "<h3>VR headset support via OpenXR</h3>"
		"This will allow Substrata to be viewable in VR headsets that work with desktop computers.";

	page += "<h3>Customisable Avatars</h3>";

	page += "<h2>About us</h2>";
	page += "<p>As mentioned above, We at Glare Technologies are the developers of <a href=\"https://www.indigorenderer.com/\">Indigo Renderer</a> and"
		" <a href=\"https://www.chaoticafractals.com/\">Chaotica Fractals</a></p>";

	page += "<p>Personally speaking, I have been developing Metaverses for around 20 years now (with some gaps), the first one dating from around 2001!</p>";

	page += "<p>I was inspired by books like Snow Crash and Neuromancer.  More recently, with the return of VR headsets and the rise in virtual worlds linked with NFT sales, and "
		" the speed of modern computers and internet connections, the time looks right to have another crack at a metaverse.</p>";

	page += " - Nicholas Chapman, founder, Glare Technologies Ltd.";


	page += "<p>Thoughts on this doc? Come discuss on our <a href=\"https://discord.com/invite/R6tfYn3\">Discord</a> or <a href=\"https://twitter.com/SubstrataVr\">Twitter</a></p>";

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


void renderBotStatusPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Bot Status");

	{ // lock scope
		Lock lock(world_state.mutex);
		page += "<h3>Screenshot bot</h3>";
		if(world_state.last_screenshot_bot_contact_time.time == 0)
			page += "No contact from screenshot bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_screenshot_bot_contact_time.time < 60)
				page += "Screenshot bot is running.  ";
			page += "Last contact from screenshot bot " + world_state.last_screenshot_bot_contact_time.timeAgoDescription();
		}

		page += "<h3>Lightmapper bot</h3>";
		if(world_state.last_lightmapper_bot_contact_time.time == 0)
			page += "No contact from lightmapper bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_lightmapper_bot_contact_time.time < 60 * 10)
				page += "Lightmapper bot is running.  ";
			page += "Last contact from lightmapper bot " + world_state.last_lightmapper_bot_contact_time.timeAgoDescription();
		}

		page += "<h3>Ethereum parcel minting bot</h3>";
		if(world_state.last_eth_bot_contact_time.time == 0)
			page += "No contact from eth bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_eth_bot_contact_time.time < 60 * 10)
				page += "Eth bot is running.  ";
			page += "Last contact from eth bot " + world_state.last_eth_bot_contact_time.timeAgoDescription();
		}
	}

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


}  // end namespace MainPageHandlers
