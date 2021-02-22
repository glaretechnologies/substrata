/*=====================================================================
MainPageHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MainPageHandlers.h"


#include <ConPrint.h>
#include "RequestInfo.h"
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"


namespace MainPageHandlers
{


const std::string CRLF = "\r\n";


void renderRootPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");
	//const bool logged_in = LoginHandlers::isLoggedInAsNick(data_store, request_info);


	const std::string deployed_version = "0.38";

	page_out +=
	"	<p>																																																		  \n"
	"	Substrata is a multi-user cyberspace/metaverse.  Chat with other users or explore objects and places that other users have created.																				  \n"
	"	<br/>																																																	  \n"
	"	You can create a free user account and add objects to the world as well!																																  \n"
	"	</p>																																																	  \n"
	"	<p>																																																		  \n"
	"	Substrata is early in development, please expect rough edges!																																			  \n"
	"	</p>																																																	  \n"
	"																																																			  \n"
	"	<h2>Downloads</h2>																																														  \n"
	"	<p>																																																		  \n"
	"	Windows - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + "_Setup.exe\">Substrata_v" + deployed_version + "_Setup.exe</a>																			  \n"
	"	</p>																																																	  \n"
	"	<p>																																																		  \n"
	"	OS X - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".pkg\">Substrata_v" + deployed_version + ".pkg</a>																						  \n"
	"	</p>																																																	  \n"
	"	<p>																																																		  \n"
	"	Linux - <a href=\"https://downloads.indigorenderer.com/dist/cyberspace/Substrata_v" + deployed_version + ".tar.gz\">Substrata_v" + deployed_version + ".tar.gz</a>																					  \n"
	"	</p>																																																	  \n"
	"																																																			  \n"
	"	<h2>Community</h2>																																														  \n"
	"																																																			  \n"
	"	<p>																																																		  \n"
	"	<a href=\"https://discord.gg/R6tfYn3\" ><img width=\"200px\" src=\"/files/join_us_on_discord.png\" /></a>																											  \n"
	"	</p>																																																	  \n"
	"																																																			  \n"
	"	<h2>CryptoVoxels</h2>																																													  \n"
	"	<p>																																																		  \n"
	"	We are currently embedding the <a href=\"https://www.cryptovoxels.com\">CryptoVoxels</a> world in Substrata, for testing and fun purposes!																  \n"
	"	</p>																																																	  \n"
	"	<p>																																																		  \n"
	"	To explore the CryptoVoxels world, just install and run Substrata, and then select from the menu bar:																									  \n"
	"<p>																																																		  \n"
	"	<b>Go &gt; Go to CryptoVoxels World</b>																																									  \n"
	"	</p>																																																	  \n"
	"	<h2>Screenshots and Videos</h2>																																											  \n"
	"																																																			  \n"
	"	<iframe width=\"650\" height=\"400\" src=\"https://www.youtube.com/embed/zAizMS16BvM\" frameborder=\"0\" allow=\"accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe>\n";


	
	
	page_out += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
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


}  // end namespace BlogHandlers
