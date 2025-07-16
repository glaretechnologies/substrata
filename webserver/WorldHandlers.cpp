/*=====================================================================
WorldHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "SubEventHandlers.h"


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


namespace WorldHandlers
{


static std::string parseWorldName(Parser& parser)
{
	const size_t startpos = parser.currentPos();
	while(!parser.eof() && (::isAlphaNumeric(parser.current()) || parser.current() == '_' || parser.current() == '/'))
	{
		parser.advance();
	}
	const std::string world_name(parser.getText() + startpos, parser.currentPos() - startpos);
	return world_name;
}


// NOTE: need to relax to have same restrictions as user name?
static void checkWorldNameValid(const std::string& world_name)
{
	for(size_t i=0; i<world_name.size(); ++i)
	{
		if(!(::isAlphaNumeric(world_name[i]) || world_name[i] == '_' || world_name[i] == '/'))
			throw glare::Exception("The character '" + std::string(1, world_name[i]) + "' is not allowed in a world name");
	}
}


static std::string URLEscapeWorldName(const std::string& world_name)
{
	// Find first slash
	std::size_t slash_pos = world_name.find_first_of('/');
	if(slash_pos == std::string::npos)
		return web::Escaping::URLEscape(world_name);
	else
	{
		std::string res = web::Escaping::URLEscape(world_name.substr(0, slash_pos)) + "/";
		if(slash_pos + 1 < world_name.size())
			res += web::Escaping::URLEscape(world_name.substr(slash_pos + 1));
		return res;
	}
}

void renderWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows a single event
{
	try
	{
		// Parse world name from request path
		Parser parser(request.path);
		if(!parser.parseString("/world/"))
			throw glare::Exception("Failed to parse /world/");

		const std::string world_name = parseWorldName(parser);
		
		std::string page;

		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			const ServerWorldState* world = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_event_owner = logged_in_user && (world->owner_id == logged_in_user->id); // If the user is logged in and created this world:

			page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/world->name, "");
			page += "<div class=\"main\">   \n";

			if(logged_in_user) // Show any messages for the user
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

			std::string owner_username;
			{
				auto res2 = world_state.user_id_to_users.find(world->owner_id);
				if(res2 != world_state.user_id_to_users.end())
					owner_username = res2->second->name;
			}

			page += "<p>Owned by " + web::Escaping::HTMLEscape(owner_username) + "</p>\n";

			page += "<p>Description:</p>\n";
			page += "<p>" + web::Escaping::HTMLEscape(world->description) + "</p>";

			if(logged_in_user_is_event_owner) // Show edit link If the user is logged in and owns this world
				page += "<br/><br/><div><a href=\"/edit_world/" + URLEscapeWorldName(world_name) + "\">Edit event</a></div>";
			
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


void renderCreateWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Create world");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			if(!logged_in_user)
			{
				page += "You must be logged in to create a world.";
			}
			else
			{
				page += "<form action=\"/create_world_post\" method=\"post\" id=\"usrform\">";
				page += "World name: <textarea rows=\"1\" cols=\"80\" name=\"world_name\" form=\"usrform\"></textarea><br/>";
				page += "<input type=\"submit\" value=\"Create world\">";
				page += "</form>";
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderEditWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		// Parse world name from request path
		Parser parser(request.path);
		if(!parser.parseString("/edit_world/"))
			throw glare::Exception("Failed to parse /edit_world/");

		const std::string world_name = parseWorldName(parser);

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit world");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup world
			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			const ServerWorldState* world = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_world_owner = logged_in_user && (world->owner_id == logged_in_user->id); // If the user is logged in and owns this world:
			if(logged_in_user_is_world_owner)
			{

				page += "<form action=\"/edit_world_post\" method=\"post\" id=\"usrform\">";
				page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->name) + "\"><br>";
				page += "Description: <textarea rows=\"30\" cols=\"80\" name=\"description\" form=\"usrform\">" + web::Escaping::HTMLEscape(world->description) + "</textarea><br>";
				page += "<input type=\"submit\" value=\"Edit world\">";
				page += "</form>";
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleCreateWorldPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name_field   = request.getPostField("world_name").str();

		if(world_name_field.empty())
			throw glare::Exception("world name cannot be empty");

		std::string new_world_name;

		{ // Lock scope
			Lock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("Access forbidden");

			checkWorldNameValid(world_name_field); // TODO: handle exceps from this more nicely

			new_world_name = logged_in_user->name + "/" + world_name_field;

			if(world_state.world_states.count(new_world_name) > 0)
				throw glare::Exception("Can not create world, a world with that name already exists.");

			Reference<ServerWorldState> world = new ServerWorldState();

			if(new_world_name.size() > ServerWorldState::MAX_NAME_SIZE)
				throw glare::Exception("invalid world name - too long");


			world->name = new_world_name;
			world->owner_id = logged_in_user->id;
			world->created_time = TimeStamp::currentTime();

			world_state.world_states.insert(std::make_pair(new_world_name, world)); // Add to world_states
			
			//world_state.addWorldAsDBDirty(world);
			world->db_dirty = true;
			world_state.markAsChanged();

			world_state.setUserWebMessage(logged_in_user->id, "Created world.");
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(new_world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCreateWorldPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditWorldPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name   = request.getPostField("world_name").str();
		const std::string description   = request.getPostField("description").str();

		if(description.size() > ServerWorldState::MAX_DESCRIPTION_SIZE)
			throw glare::Exception("invalid world description - too long");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup world
			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			ServerWorldState* world = res->second.ptr();

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(logged_in_user && (world->owner_id == logged_in_user->id)) // If the user is logged in and owns this world:
			{
				world->description = description;

				//world_state.addWorldAsDBDirty(world);
				world->db_dirty = true;
				world_state.markAsChanged();

				world_state.setUserWebMessage(logged_in_user->id, "Updated world.");
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditWorldPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace WorldHandlers
