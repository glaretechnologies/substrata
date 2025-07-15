/*=====================================================================
NewsPostHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "NewsPostHandlers.h"


#include "RequestInfo.h"
#include "WebDataStore.h"
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
#include <FileUtils.h>


namespace NewsPostHandlers
{


void renderNewsPostPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows a single news post
{
	try
	{
		// Parse post id from request path
		Parser parser(request.path);
		if(!parser.parseString("/news_post/"))
			throw glare::Exception("Failed to parse /news_post/");

		uint32 post_id;
		if(!parser.parseUnsignedInt(post_id))
			throw glare::Exception("Failed to parse post id");

		
		std::string page;

		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.news_posts.find(post_id);
			if(res == world_state.news_posts.end())
				throw glare::Exception("Couldn't find news post");

			const NewsPost* post = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_post_owner = logged_in_user && (post->creator_id == logged_in_user->id); // If the user is logged in and created this post:

			if(post->state == NewsPost::State_published)
			{
				page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/post->title, "");
				page += "<div class=\"main\">   \n";

				if(logged_in_user) // Show any messages for the user
				{
					const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
					if(!msg.empty())
						page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
				}

				page += "<div class=\"news-post-timestamp\">" + post->created_time.dayString() + "</div>";
				page += "<div class=\"news-post-content\">\n";
				page += post->content; // Insert unescaped content!
				page += "</div>\n";


				page += "<a href=\"/news\">See all news</a> &gt;\n";
			}
			else // else if state is draft or deleted.
			{
				if(logged_in_user_is_post_owner)
				{
					// If the logged-in user created this post, show it
					page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/post->title, "");
					page += "<div class=\"main\">   \n";

					if(logged_in_user) // Show any messages for the user
					{
						const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
						if(!msg.empty())
							page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
					}
					
					page += "<b>State: " + NewsPost::stateString(post->state) + "</b>\n";
					page += "<div class=\"news-post-timestamp\">" + post->created_time.dayString() + "</div>";
					page += "<div class=\"news-post-content\">\n";
					page += post->content; // Insert unescaped content!
					page += "</div>\n";
				}
				else
				{
					// Otherwise draft or deleted posts are not visible.
					page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Post not found", "");
					page += "<div class=\"main\">   \n";
					page += "No such post found.\n";
				}
			}

			if(logged_in_user_is_post_owner) // Show edit link If the user is logged in and owns this parcel
				page += "<div><a href=\"/edit_news_post?post_id=" + toString(post->id) + "\">Edit post</a></div>";
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


void renderAllNewsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Latest news");

		// Get start post index for pagination from URL
		const int start = request.isURLParamPresent("start") ? request.getURLIntParam("start") : 0;

		page += "<div class=\"main\">   \n";

		const int max_num_to_display = 5;

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Advance to 'start' offset.  Use reverse iterators to show most recent posts first.
			auto it = world_state.news_posts.rbegin();
			for(int i=0; it != world_state.news_posts.rend() && i < start; ++it, ++i)
			{}

			int num_displayed = 0;
			for(; it != world_state.news_posts.rend() && num_displayed < max_num_to_display; ++it)
			{
				const NewsPost* post = it->second.ptr();
				if(post->state == NewsPost::State_published)
				{
					page += "<h2><a href=\"/news_post/" + toString(post->id) + "\">" + post->title + "</a></h2>"; // Insert unescaped content!
					page += "<div class=\"news-post-timestamp\">" + post->created_time.dayString() + "</div>";
					page += "<div class=\"news-post-content\">\n";
					page += post->content; // Insert unescaped content!
					//page += web::ResponseUtils::getPrefixWithStrippedTags(post->content, 200); // Insert unescaped content!
					page += "</div>\n";

					page += "<br/>\n";

					num_displayed++;
				}
			}

			// Show 'newer posts' link if there are any newer posts.
			if(start > 0)
				page += "<a href=\"/news?start=" + toString(myMax(0, start - max_num_to_display)) + "\">&lt; Newer posts</a>\n";
		
			// Show 'older posts' link if there are any older posts.
			const int next_start = start + max_num_to_display;
			const int num_older_posts_remaining = (int)world_state.news_posts.size() - next_start;
			if(num_older_posts_remaining > 0)
			{
				if(start > 0)
					page += " | ";
				page += "<a href=\"/news?start=" + toString(next_start) + "\">Older posts &gt;</a>  \n";
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


void renderEditNewsPostPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const int post_id = request.getURLIntParam("post_id");

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit news post");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.news_posts.find(post_id);
			if(res != world_state.news_posts.end())
			{
				const NewsPost* news_post = res->second.ptr();

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

				if(logged_in_user)
				{
					const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
					if(!msg.empty())
						page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
				}


				const bool logged_in_user_is_post_owner = logged_in_user && (news_post->creator_id == logged_in_user->id); // If the user is logged in and created this post:
				if(logged_in_user_is_post_owner)
				{
					page += "<form action=\"/edit_news_post_post\" method=\"post\" id=\"usrform\" enctype=\"multipart/form-data\">";
					page += "<input type=\"hidden\" name=\"post_id\" value=\"" + toString(post_id) + "\"><br>";
					page += "Title: <textarea rows=\"1\" cols=\"80\" name=\"title\" form=\"usrform\">"    + web::Escaping::HTMLEscape(news_post->title) +   "</textarea><br>";
					page += "Content: <textarea rows=\"30\" cols=\"80\" name=\"content\" form=\"usrform\">" + web::Escaping::HTMLEscape(news_post->content) + "</textarea><br>";
					page += "Thumbnail URL: <textarea rows=\"1\" cols=\"80\" name=\"thumbnail-url\" form=\"usrform\">"    + web::Escaping::HTMLEscape(news_post->thumbnail_URL) +   "</textarea><br>";
					page += "Attach image: <input type=\"file\" name=\"file\" value=\"\"><br>";
					page += std::string("Published: <input type=\"checkbox\" name=\"published\" value=\"checked\" ") + ((news_post->state == NewsPost::State_published) ? "checked" : "") + "><br/>\n";
					page += "<input type=\"submit\" value=\"Update news post\">";
					page += "</form>";
				}
				else
					throw glare::Exception("Access forbidden");
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


static std::string sanitiseString(const std::string& s)
{
	std::string res = s;
	for(size_t i=0; i<s.size(); ++i)
	{
		if(!(::isAlphaNumeric(s[i]) || (s[i] == '_')))
			res[i] = '_';
	}
	return res;
}


static std::string sanitiseFilename(const std::string& s)
{
	const std::string::size_type dot_index = s.find_last_of('.');

	if(dot_index == std::string::npos)
		return sanitiseString(s);
	else
		return sanitiseString(s.substr(0, dot_index)) + "." + sanitiseString(getTailSubString(s, dot_index + 1));
}


void handleEditNewsPostPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int post_id = request.getPostIntField("post_id");
		const web::UnsafeString new_title   = request.getPostField("title");
		const web::UnsafeString new_content = request.getPostField("content");
		const web::UnsafeString new_thumbnail_URL = request.getPostField("thumbnail-url");
		const bool new_published = request.getPostField("published") == "checked";

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.news_posts.find(post_id);
			if(res != world_state.news_posts.end())
			{
				NewsPost* news_post = res->second.ptr();

				const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && (news_post->creator_id == logged_in_user->id)) // If the user is logged in and created this post:
				{
					news_post->title = new_title.str();
					news_post->content = new_content.str();
					news_post->thumbnail_URL = new_thumbnail_URL.str();
					news_post->last_modified_time = TimeStamp::currentTime();
					news_post->state = new_published ? NewsPost::State_published : NewsPost::State_draft;

					world_state.addNewsPostAsDBDirty(news_post);

					world_state.markAsChanged();


					std::string msg_to_user;

					// Save any posted images to disk
					Reference<web::FormField> file_field = request.getPostFieldForNameIfPresent("file");
					if(file_field && !file_field->filename.empty() && !file_field->content.empty())
					{
						const std::string sanitised_filename = sanitiseFilename(file_field->filename.str());
						const std::string write_path = world_state.web_data_store->public_files_dir + "/" + sanitised_filename;
						if(FileUtils::fileExists(write_path))
							throw glare::Exception("File already exists at location '" + write_path + "', not overwriting.");
						FileUtils::writeEntireFile(write_path, (const char*)file_field->content.data(), file_field->content.size());

						msg_to_user += "Saved attachment to '" + write_path + "'.";
					}


					world_state.setUserWebMessage(logged_in_user->id, "Updated news post contents. " + msg_to_user);
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/news_post/" + toString(post_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditNewsPostPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteNewsPostPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int post_id = request.getPostIntField("post_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup post
			const auto res = world_state.news_posts.find(post_id);
			if(res != world_state.news_posts.end())
			{
				NewsPost* news_post = res->second.ptr();

				const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
				if(logged_in_user && (news_post->creator_id == logged_in_user->id)) // If the user is logged in and created this post:
				{
					//world_state.news_posts.erase(post_id);
					world_state.setUserWebMessage(logged_in_user->id, "Deleted post.");

					news_post->state = NewsPost::State_deleted;

					world_state.addNewsPostAsDBDirty(news_post);

					world_state.markAsChanged();
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/news_post/" + toString(post_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteNewsPostPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace NewsPostHandlers
