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
#include <WebDataStore.h>
#include <webserver/ResponseUtils.h>


namespace MainPageHandlers
{


void renderRootPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	//std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");
	//const bool logged_in = LoginHandlers::isLoggedInAsNick(data_store, request_info);

	std::string page_out = WebServerResponseUtils::standardHTMLHeader(data_store, request_info, /*page title=*/"Substrata");
	page_out +=
		"	<body class=\"root-body\">\n"
		"	<div id=\"login\">\n"; // Start login div

	web::UnsafeString logged_in_username;
	bool is_user_admin;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username, is_user_admin);

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


	//page_out += "<img src=\"/files/logo_main_page.png\" alt=\"substrata logo\" class=\"logo-root-page\" />";


	std::string auction_html, latest_news_html, events_html, photos_html;
	{ // lock scope
		WorldStateLock lock(world_state.mutex);

		ServerWorldState* root_world = world_state.getRootWorldState().ptr();

		int num_auctions_shown = 0; // Num substrata auctions shown
		const TimeStamp now = TimeStamp::currentTime();
		auction_html += "<div class=\"root-auction-list-container\">\n";
		const ServerWorldState::ParcelMapType& parcels = root_world->getParcels(lock);
		for(auto it = parcels.begin(); (it != parcels.end()) && (num_auctions_shown < 4); ++it)
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

							auction_html += "<div class=\"root-auction-div\"><a href=\"/parcel_auction/" + toString(auction_id) + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" class=\"root-auction-thumbnail\" alt=\"screenshot\" /></a>  <br/>"
								"&euro;" + doubleToStringNDecimalPlaces(cur_price_EUR, 2) + " / " + doubleToStringNSigFigs(cur_price_BTC, 2) + "&nbsp;BTC / " + doubleToStringNSigFigs(cur_price_ETH, 2) + "&nbsp;ETH</div>";
						}

						num_auctions_shown++;
					}
				}
			}
		}
		auction_html += "</div>\n";

		// If no auctions on substrata site were shown, show OpenSea auctions, if any.
		int opensea_num_shown = 0;
		if(num_auctions_shown == 0)
		{
			auction_html += "<div class=\"root-auction-list-container\">\n";
			for(auto it = world_state.opensea_parcel_listings.begin(); (it != world_state.opensea_parcel_listings.end()) && (opensea_num_shown < 3); ++it)
			{
				const OpenSeaParcelListing& listing = *it;

				auto parcel_res = root_world->getParcels(lock).find(listing.parcel_id); // Look up parcel
				if(parcel_res != root_world->getParcels(lock).end())
				{
					const Parcel* parcel = parcel_res->second.ptr();

					if(parcel->screenshot_ids.size() >= 1)
					{
						const uint64 shot_id = parcel->screenshot_ids[0]; // Close-in screenshot

						const std::string opensea_url = "https://opensea.io/assets/ethereum/0xa4535f84e8d746462f9774319e75b25bc151ba1d/" + listing.parcel_id.toString();

						auction_html += "<div class=\"root-auction-div\"><a href=\"/parcel/" + parcel->id.toString() + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" class=\"root-auction-thumbnail\" alt=\"screenshot\" /></a>  <br/>"
							"<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a> <a href=\"" + opensea_url + "\">View&nbsp;on&nbsp;OpenSea</a></div>";
					}

					opensea_num_shown++;
				}
			}
			auction_html += "</div>\n";
		}

		if(num_auctions_shown == 0 && opensea_num_shown == 0)
			auction_html += "<p>Sorry, there are no parcels for sale here right now.  Please check back later!</p>";



		// Build latest news HTML
		latest_news_html += "<div class=\"root-news-div-container\">\n";		const int max_num_to_display = 4;
		int num_displayed = 0;
		for(auto it = world_state.news_posts.rbegin(); it != world_state.news_posts.rend() && num_displayed < max_num_to_display; ++it)
		{
			NewsPost* post = it->second.ptr();

			if(post->state == NewsPost::State_published)
			{
				latest_news_html += "<div class=\"root-news-div\">";

				const std::string post_url = "/news_post/" + toString(post->id);

				if(post->thumbnail_URL.empty())
					latest_news_html += "<div class=\"root-news-thumb-div\"><a href=\"" + post_url + "\"><img src=\"/files/default_thumb.jpg\" class=\"root-news-thumbnail\" /></a></div>";
				else
					latest_news_html += "<div class=\"root-news-thumb-div\"><a href=\"" + post_url + "\"><img src=\"" + post->thumbnail_URL + "\" class=\"root-news-thumbnail\" /></a></div>";

				latest_news_html += "<div class=\"root-news-title\"><a href=\"" + post_url + "\">" + post->title + "</a></div>";
				//latest_news_html += "<div class=\"root-news-content\"><a href=\"" + post_url + "\">" + web::ResponseUtils::getPrefixWithStrippedTags(post->content, /*max len=*/200) + "</a></div>";

				latest_news_html += "</div>";

				num_displayed++;
			}
		}
		latest_news_html += "</div>\n";


		// Build events HTML
		events_html += "<div class=\"root-events-div-container\">\n";		const int max_num_events_to_display = 4;
		int num_events_displayed = 0;
		for(auto it = world_state.events.rbegin(); (it != world_state.events.rend()) && (num_events_displayed < max_num_events_to_display); ++it)
		{
			const SubEvent* event = it->second.ptr();

			// We don't want to show old events, so end time has to be in the future, or sometime today, e.g. end_time >= (current time - 24 hours)
			const TimeStamp min_end_time(TimeStamp::currentTime().time - 24 * 3600);
			if((event->end_time >= min_end_time) && (event->state == SubEvent::State_published))
			{
				events_html += "<div class=\"root-event-div\">";

				events_html += "<div class=\"root-event-title\"><a href=\"/event/" + toString(event->id) + "\">" + web::Escaping::HTMLEscape(event->title) + "</a></div>";

				events_html += "<div class=\"root-event-description\">";
				const size_t MAX_DESCRIP_SHOW_LEN = 80;
				events_html += web::Escaping::HTMLEscape(event->description.substr(0, MAX_DESCRIP_SHOW_LEN));
				if(event->description.size() > MAX_DESCRIP_SHOW_LEN)
					events_html += "...";
				events_html += "</div>";

				events_html += "<div class=\"root-event-time\">" + event->start_time.dayAndTimeStringUTC() + "</div>";

				events_html += "</div>";

				num_events_displayed++;
			}
		}
		if(num_events_displayed == 0)
			events_html += "There are no upcoming events.  Create one!";
		events_html += "</div>\n";


		//------------------------------- Build photos grid view HTML --------------------------
		photos_html += "<div class=\"photo-container\">\n";		const int max_num_photos_to_display = 20;
		int num_photos_displayed = 0;
		for(auto it = world_state.photos.rbegin(); (it != world_state.photos.rend()) && (num_photos_displayed < max_num_photos_to_display); ++it)
		{
			const Photo* photo = it->second.ptr();
			if(photo->state == Photo::State_published)
			{
				const std::string thumb_URL = "/photo_thumb_image/" + toString(photo->id);
				photos_html += "<a href=\"/photo/" + toString(photo->id) + "\"><img src=\"" + thumb_URL + "\" class=\"root-photo-img\"/></a>";

				num_photos_displayed++;
			}
		}

		photos_html += "</div>\n";

	} // end lock scope


	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("root_page.htmlfrag");
	if(store_file.nonNull())
	{
		page_out += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	StringUtils::replaceFirstInPlace(page_out, "LATEST_NEWS_HTML", latest_news_html);

	StringUtils::replaceFirstInPlace(page_out, "LAND_PARCELS_FOR_SALE_HTML", auction_html);

	StringUtils::replaceFirstInPlace(page_out, "EVENTS_HTML", events_html);

	StringUtils::replaceFirstInPlace(page_out, "PHOTOS_HTML", photos_html);

	page_out += "<script src=\"/files/root-page.js\"></script>";
	
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
"	JPG, PNG, GIF, TIF, EXR, KTX, KTX2</span></p>																"
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


void renderAboutScripting(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Scripting in Substrata");
	
	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("about_scripting.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderGenericPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const GenericPage& generic_page, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/generic_page.page_title);
	
	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile(generic_page.fragment_path);
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAboutSubstrataPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Our vision for Substrata");

	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("about_substrata.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderRunningYourOwnServerPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Running your own Substrata Server");

	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("running_your_own_server.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}
	
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


void renderMapPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	const std::string extra_header_tags = WebServerResponseUtils::getMapHeaderTags();
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Map", extra_header_tags);

	page += WebServerResponseUtils::getMapEmbedCode(world_state, /*highlighted_parcel_id=*/ParcelID::invalidParcelID());

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


} // end namespace MainPageHandlers
