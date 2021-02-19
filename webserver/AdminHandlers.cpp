/*=====================================================================
AdminHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AdminHandlers.h"


#include <ConPrint.h>
#include "RequestInfo.h"
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
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"


namespace AdminHandlers
{


void renderMainAdminPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(request_info, /*page title=*/"Substrata");
	
	web::UnsafeString logged_in_username;
	const bool logged_in = LoginHandlers::isLoggedIn(request_info, logged_in_username);
	if(logged_in_username.str() != "Ono-Sendai")
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
			page_out += "id: " + parcel->id.toString() + ",       owner: " + web::Escaping::HTMLEscape(owner_username) + ",       description: " + web::Escaping::HTMLEscape(parcel->description) + 
				",      created " + parcel->created_time.timeAgoDescription();
			page_out += "</div>\n";
		}

	} // End Lock scope

	page_out += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


} // end namespace AdminHandlers
