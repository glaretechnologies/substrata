/*=====================================================================
SubEventHandlers.cpp
--------------------
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


namespace SubEventHandlers
{


void renderEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows a single event
{
	try
	{
		// Parse event id from request path
		Parser parser(request.path);
		if(!parser.parseString("/event/"))
			throw glare::Exception("Failed to parse /event/");

		uint64 event_id;
		if(!parser.parseUInt64(event_id))
			throw glare::Exception("Failed to parse event id");

		
		std::string page;

		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.events.find(event_id);
			if(res == world_state.events.end())
				throw glare::Exception("Couldn't find event");

			const SubEvent* event = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_event_owner = logged_in_user && (event->creator_id == logged_in_user->id); // If the user is logged in and created this event:

			if((event->state == SubEvent::State_published) || logged_in_user_is_event_owner)
			{
				page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/event->title, "");
				page += "<div class=\"main\">   \n";

				if(logged_in_user) // Show any messages for the user
				{
					const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
					if(!msg.empty())
						page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
				}

				std::string creator_username;
				{
					auto res2 = world_state.user_id_to_users.find(event->creator_id);
					if(res2 != world_state.user_id_to_users.end())
						creator_username = res2->second->name;
				}

				page += "<table>\n"; // Event data table

				if(logged_in_user_is_event_owner)
					page += "<tr><td class=\"event-datum-title\">Status:</td><td class=\"event-datum\"><b>" + SubEvent::stateString(event->state) + "</b></td></tr>\n";

				if(event->world_name.empty()) // If in main world:
				{
					const std::string parcel_description = event->parcel_id.valid() ? ("Parcel " + event->parcel_id.toString()) : "Unspecified parcel";
					page += "<tr><td class=\"event-datum-title\">Location:</td><td class=\"event-datum\"><a href=\"/parcel/" + event->parcel_id.toString() + "\"> " + parcel_description + "</a></td></tr>\n";
				}
				else // Else if in personal world:
				{
					page += "<tr><td class=\"event-datum-title\">Location:</td><td class=\"event-datum\">" + web::Escaping::HTMLEscape(creator_username) + "'s personal world</td></tr>\n";
				}


				page += "<tr><td class=\"event-datum-title\">Start date and time:</td><td class=\"event-datum\">" + event->start_time.dayAndTimeStringUTC() + " UTC (" + event->start_time.timeDescription() + ")</td></tr>";

				const std::string duration_str = (event->end_time.time >= event->start_time.time) ? 
					TimeStamp::durationDescription((int)(event->end_time.time - event->start_time.time)) :
					"unknown";
				page += "<tr><td class=\"event-datum-title\">Duration:</td><td class=\"event-datum\">" + duration_str + "</td></tr>";

				
				page += "<tr><td class=\"event-datum-title\">Created by:</td><td class=\"event-datum\">" + web::Escaping::HTMLEscape(creator_username) + "</td></tr>\n";

				page += "</table>\n";

				page += "<h3 class=\"event-description-header\">Description</h3>\n";
				page += "<div class=\"event-description\">\n";
				page += web::Escaping::HTMLEscape(event->description);
				page += "</div>\n";

				if(logged_in_user_is_event_owner)
				{
					// Show attendee list
					page += "<h3>Attendees</h3>\n";
					page += toString(event->attendee_ids.size()) + " user(s) visited the party while it was happening:\n";
					page += "(The Attendees section is only visible to the event creator)";

					for(auto it = event->attendee_ids.begin(); it != event->attendee_ids.end(); ++it)
					{
						std::string attendee_username;
						{
							auto res3 = world_state.user_id_to_users.find(*it);
							if(res3 != world_state.user_id_to_users.end())
								attendee_username = res3->second->name;
						}
						page += "<div>" + web::Escaping::HTMLEscape(attendee_username) + "<div>";
					}
				}
			}
			else
			{
				// Otherwise draft or deleted events are not visible.
				page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Event not found", "");
				page += "<div class=\"main\">   \n";
				page += "No such event found.\n";
			}

			if(logged_in_user_is_event_owner) // Show edit link If the user is logged in and owns this parcel
				page += "<br/><br/><div><a href=\"/edit_event?event_id=" + toString(event->id) + "\">Edit event</a></div>";
		} // end lock scope

		page += "<br/><br/><a href=\"/events\">See all events</a> &gt;\n";

		page += "</div>   \n"; // end main div
		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderAllEventsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Latest events");

		// Get start post index for pagination from URL
		const int start = request.isURLParamPresent("start") ? request.getURLIntParam("start") : 0;

		page += "<div class=\"main\">   \n";

		const int max_num_to_display = 8;

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Advance to 'start' offset.  Use reverse iterators to show most recent posts first.
			auto it = world_state.events.rbegin();
			for(int i=0; it != world_state.events.rend() && i < start; ++it, ++i)
			{}

			int num_displayed = 0;
			for(; it != world_state.events.rend() && num_displayed < max_num_to_display; ++it)
			{
				const SubEvent* event = it->second.ptr();
				if(event->state == SubEvent::State_published)
				{
					page += "<h2 class=\"event-title\"><a href=\"/event/" + toString(event->id) + "\">" + web::Escaping::HTMLEscape(event->title) + "</a></h2>";

					page += "<div class=\"event-description\">";
					const size_t MAX_DESCRIP_SHOW_LEN = 80;
					page += web::Escaping::HTMLEscape(event->description.substr(0, MAX_DESCRIP_SHOW_LEN));
					if(event->description.size() > MAX_DESCRIP_SHOW_LEN)
						page += "...";
					page += "</div>";
					
					page += "<div class=\"event-time\">" + event->start_time.dayAndTimeStringUTC() + "</div>";

					num_displayed++;
				}
			}

			page += "<br/><br/>\n";

			// Show 'newer events' link if there are any newer posts.
			if(start > 0)
				page += "<a href=\"/events?start=" + toString(myMax(0, start - max_num_to_display)) + "\">&lt; Newer events</a>\n";
		
			// Show 'older events' link if there are any older posts.
			const int next_start = start + max_num_to_display;
			const int num_older_events_remaining = (int)world_state.events.size() - next_start;
			if(num_older_events_remaining > 0)
			{
				if(start > 0)
					page += " | ";
				page += "<a href=\"/events?start=" + toString(next_start) + "\">Older events &gt;</a>  \n";
			}

		} // end lock scope

		page += "<br/><div><a href=\"/create_event\">Create an event</a></div>";

		page += "</div>   \n"; // end main div
		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderCreateEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Create event");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			if(!logged_in_user)
			{
				page += "You must be logged in to create an event.";
			}
			else
			{
				page += "<form action=\"/create_event_post\" method=\"post\" id=\"usrform\">";
				page += "Title: <textarea rows=\"1\" cols=\"80\" name=\"title\" form=\"usrform\"></textarea><br/>";
				page += "Description: <textarea rows=\"30\" cols=\"80\" name=\"description\" form=\"usrform\"></textarea><br/>";
				page += "World (leave empty for main world): <textarea rows=\"1\" cols=\"80\" name=\"world_name\" form=\"usrform\"></textarea><br/>";
				page += "Parcel number: <input type=\"number\" name=\"parcel_id\" value=\"0\"><br/>";
				page += "Start time (UTC): <input type=\"datetime-local\" name=\"start_time\"><br/>";
				page += "End time (UTC): <input type=\"datetime-local\" name=\"end_time\"><br/>";
				//page += "timezone offset: <input id=\"timezone-offset-input\" type=\"hidden\" name=\"timezone_offset\" value=\"\"><br/>";
				page += "<input type=\"submit\" value=\"Create event\">";
				page += "</form>";
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		//page += "<script src=\"/files/timezoneoffset.js\"></script>";

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderEditEventPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const int event_id = request.getURLIntParam("event_id");

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit event");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup event
			const auto res = world_state.events.find(event_id);
			if(res != world_state.events.end())
			{
				const SubEvent* event = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				const bool logged_in_user_is_event_owner = logged_in_user && (event->creator_id == logged_in_user->id); // If the user is logged in and created this event:
				if(logged_in_user_is_event_owner)
				{

					const std::string start_time_str = event->start_time.HTTPDateTimeFormattedStringUTC();
					const std::string end_time_str   = event->end_time.HTTPDateTimeFormattedStringUTC();

					page += "<form action=\"/edit_event_post\" method=\"post\" id=\"usrform\">";
					page += "<input type=\"hidden\" name=\"event_id\" value=\"" + toString(event_id) + "\"><br>";
					page += "Title: <textarea rows=\"1\" cols=\"80\" name=\"title\" form=\"usrform\">"    + web::Escaping::HTMLEscape(event->title) +   "</textarea><br>";
					page += "Description: <textarea rows=\"30\" cols=\"80\" name=\"description\" form=\"usrform\">" + web::Escaping::HTMLEscape(event->description) + "</textarea><br>";
					page += "World (leave empty for main world): <textarea rows=\"1\" cols=\"80\" name=\"world_name\" form=\"usrform\">" + web::Escaping::HTMLEscape(event->world_name) +"</textarea><br/>";
					page += "Parcel number: <input type=\"number\" name=\"parcel_id\" value=\"" + event->parcel_id.toString() + "\"><br/>";
					page += "Start time (UTC): <input type=\"datetime-local\" name=\"start_time\" value=\"" + start_time_str + "\"><br/>";
					page += "End time (UTC): <input type=\"datetime-local\" name=\"end_time\" value=\"" + end_time_str + "\"><br/>";
					//page += "timezone offset: <input id=\"timezone-offset-input\" type=\"hidden\" name=\"timezone_offset\" value=\"\"><br/>";
					page += std::string("Published: <input type=\"checkbox\" name=\"published\" value=\"checked\" ") + ((event->state == SubEvent::State_published) ? "checked" : "") + "><br/>\n";
					page += "<input type=\"submit\" value=\"Edit event\">";
					page += "</form>";
				}
				else
					throw glare::Exception("Access forbidden");
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		//page += "<script src=\"/files/timezoneoffset.js\"></script>";

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


static TimeStamp parseHTTPDateTime(const web::UnsafeString& str)
{
	Parser parser(str.str());

	// parse year
	int year;
	if(!parser.parseInt(year))
		throw glare::Exception("Failed to parse year.");

	if(!parser.parseChar('-'))
		throw glare::Exception("Failed to parse '-'.");

	// parse month
	int month;
	if(!parser.parseInt(month))
		throw glare::Exception("Failed to parse month.");

	if(!parser.parseChar('-'))
		throw glare::Exception("Failed to parse '-'.");

	// parse day
	int day;
	if(!parser.parseInt(day))
		throw glare::Exception("Failed to parse day.");

	// Parse 'T' or space separating date and time
	if(parser.currentIsChar('T') || parser.currentIsChar(' '))
		parser.advance();
	else
		throw glare::Exception("Expected 'T' or space");

	// parse hour
	int hour;
	if(!parser.parseInt(hour))
		throw glare::Exception("Failed to parse hour.");

	if(!parser.parseChar(':'))
		throw glare::Exception("Failed to parse ':'.");

	// parse minute
	int minute;
	if(!parser.parseInt(minute))
		throw glare::Exception("Failed to parse minute.");

	// don't parse optional seconds for now.

	return TimeStamp::fromComponents(year, month - 1, day, hour, minute, 0);
}


static std::string capStringSize(const std::string& s_, size_t max_size)
{
	std::string s = s_;
	if(s.size() > max_size)
		s = s.substr(0, max_size);
	return s;
}


void handleCreateEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString new_world_name   = request.getPostField("world_name");
		const ParcelID parcel_id = !request.getPostField("parcel_id").empty() ? ParcelID(request.getPostIntField("parcel_id")) : ParcelID();
		const web::UnsafeString new_title   = request.getPostField("title");
		const web::UnsafeString new_description = request.getPostField("description");
		const web::UnsafeString start_time_str = request.getPostField("start_time");
		const web::UnsafeString end_time_str = request.getPostField("end_time");

		const TimeStamp start_time_UTC = start_time_str.empty() ? TimeStamp::currentTime() : parseHTTPDateTime(start_time_str);
		const TimeStamp end_time_UTC   = end_time_str.empty()   ? TimeStamp::currentTime() : parseHTTPDateTime(end_time_str);

		uint64 new_event_id = 0;

		{ // Lock scope
			Lock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("Access forbidden");

			SubEventRef event = new SubEvent();

			new_event_id = world_state.getNextEventUID();
			event->id = new_event_id;
			event->world_name = capStringSize(new_world_name.str(),   SubEvent::MAX_WORLD_NAME_SIZE);
			event->parcel_id = parcel_id;
			event->creator_id = logged_in_user->id;
			event->created_time = TimeStamp::currentTime();
			event->last_modified_time = event->created_time;
			event->start_time = start_time_UTC;
			event->end_time = end_time_UTC;
			event->title       = capStringSize(new_title.str(),       SubEvent::MAX_TITLE_SIZE);
			event->description = capStringSize(new_description.str(), SubEvent::MAX_DESCRIPTION_SIZE);
			event->state = SubEvent::State_draft;

			world_state.events.insert(std::make_pair(event->id, event)); // Add to world
			
			world_state.addEventAsDBDirty(event);

			world_state.setUserWebMessage(logged_in_user->id, "Created draft event.");
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/event/" + toString(new_event_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCreateEventPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int event_id = request.getPostIntField("event_id");
		const web::UnsafeString new_world_name   = request.getPostField("world_name");
		const ParcelID parcel_id = !request.getPostField("parcel_id").empty() ? ParcelID(request.getPostIntField("parcel_id")) : ParcelID();
		const web::UnsafeString new_title   = request.getPostField("title");
		const web::UnsafeString new_description = request.getPostField("description");
		const web::UnsafeString start_time_str = request.getPostField("start_time");
		const web::UnsafeString end_time_str = request.getPostField("end_time");
		//const int timezone_offset = request.getPostIntField("timezone_offset"); // In minutes
		const bool new_published = request.getPostField("published") == "checked";

		const TimeStamp start_time_UTC = start_time_str.empty() ? TimeStamp::currentTime() : parseHTTPDateTime(start_time_str);
		const TimeStamp end_time_UTC   = end_time_str.empty()   ? TimeStamp::currentTime() : parseHTTPDateTime(end_time_str);

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.events.find(event_id);
			if(res != world_state.events.end())
			{
				SubEvent* event = res->second.ptr();

				const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && (event->creator_id == logged_in_user->id)) // If the user is logged in and created this event:
				{
					event->world_name =  capStringSize(new_world_name.str(),  SubEvent::MAX_WORLD_NAME_SIZE);
					event->parcel_id = parcel_id;
					event->title =       capStringSize(new_title.str(),       SubEvent::MAX_TITLE_SIZE);
					event->description = capStringSize(new_description.str(), SubEvent::MAX_DESCRIPTION_SIZE);
					event->start_time = start_time_UTC;
					event->end_time = end_time_UTC;
					event->last_modified_time = TimeStamp::currentTime();
					event->state = new_published ? SubEvent::State_published : SubEvent::State_draft;

					world_state.addEventAsDBDirty(event);

					world_state.setUserWebMessage(logged_in_user->id, "Updated event.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/event/" + toString(event_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditEventPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteEventPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int event_id = request.getPostIntField("event_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.events.find(event_id);
			if(res != world_state.events.end())
			{
				SubEvent* event = res->second.ptr();

				const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && (event->creator_id == logged_in_user->id)) // If the user is logged in and created this event:
				{
					world_state.setUserWebMessage(logged_in_user->id, "Deleted event.");

					event->state = SubEvent::State_deleted;

					world_state.addEventAsDBDirty(event);

					world_state.markAsChanged();
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/events");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteEventPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace SubEventHandlers
