/*=====================================================================
LoginHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoginHandlers.h"


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
#include <SHA256.h>
#include <Base64.h>
#include <CryptoRNG.h>
#include <MemMappedFile.h>
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "../server/ServerWorldState.h"
#include "../server/UserWebSession.h"


namespace LoginHandlers
{


const std::string CRLF = "\r\n";


/*
Cookies
-------
site-b cookie currently stores session ID.
*/


bool isLoggedIn(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out)
{
	Lock lock(world_state.mutex);

	const User* user = getLoggedInUser(world_state, request_info);
	if(user == NULL)
	{
		logged_in_username_out = "";
		return false;
	}
	else
	{
		logged_in_username_out = user->name;
		return true;
	}
}


bool loggedInUserHasAdminPrivs(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	web::UnsafeString logged_in_username;
	const bool logged_in = isLoggedIn(world_state, request_info, logged_in_username);
	return logged_in && (logged_in_username.str() == "Ono-Sendai");
}


// ServerAllWorldsState should be locked
User* getLoggedInUser(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	for(size_t i=0; i<request_info.cookies.size(); ++i)
	{
		if(request_info.cookies[i].key == "site-b")
		{
			try
			{
				// Lookup session
				const auto res = world_state.user_web_sessions.find(request_info.cookies[i].value);
				if(res == world_state.user_web_sessions.end())
					return NULL; // Session not found
				else
				{
					const UserWebSession* session = res->second.ptr();

					// Lookup user from session
					const auto user_res = world_state.user_id_to_users.find(session->user_id);
					if(user_res == world_state.user_id_to_users.end())
						return NULL; // User not found
					else
						return user_res->second.ptr();
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Error: " + e.what());
			}
		}
	}

	return NULL;
}


void renderLoginPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Login to Substrata");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Log in</h1><body>";

	if(!msg.empty())
		page_out += "<div class=\"msg\" style=\"background-color: yellow\">" + msg.HTMLEscaped() + "</div>  \n";

	page_out += "<form action=\"login_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"return\" value=\"" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\"><br>";
	page_out += "username: <input type=\"text\" name=\"username\"><br>";
	page_out += "password: <input type=\"password\" name=\"password\"><br/>";
	page_out += "<input type=\"submit\" value=\"Submit\">";
	page_out += "</form>";

	page_out += "<br/><br/><br/>";
	page_out += "<div>Don't have an account?  <a href=\"/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\">Sign up</a>";

	page_out += "</body></html>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


bool isSafeReturnURL(const std::string& URL)
{
	if(!::hasPrefix(URL, "/")) // Only allow absolute paths with no domain etc.. (don't want to redirect to another site)
		return false;

	for(size_t i=0; i<URL.size(); ++i)
		if(!(isAlphaNumeric(URL[i]) || URL[i] == '_' || URL[i] == '/'))
			return false;

	return true;
}


void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		// Try and log in user
		const web::UnsafeString username = request_info.getPostField("username");
		const web::UnsafeString password = request_info.getPostField("password");
		const web::UnsafeString raw_return_URL = request_info.getPostField("return");

		conPrint("username: '" + username.str() + "'");
		conPrint("password: '" + password.str() + "'");
		conPrint("raw_return_URL: '" + raw_return_URL.str() + "'");

		std::string return_URL = raw_return_URL.str();
		if(return_URL.empty())
			return_URL = "/";

		// See if we have a URL to return to after logging in.
		if(!isSafeReturnURL(return_URL))
			throw glare::Exception("Invalid return URL.");

		bool valid_username_and_credentials = false;
		std::string session_id;
		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup user by username
			const auto res = world_state.name_to_users.find(username.str());
			if(res != world_state.name_to_users.end())
			{
				// Found user for username
				const User& user = *(res->second);
				if(user.isPasswordValid(password.str()))
				{
					valid_username_and_credentials = true;

					UserWebSessionRef session = new UserWebSession();
					session->id = UserWebSession::generateRandomKey();
					session->user_id = user.id;
					session->created_time = TimeStamp::currentTime();

					world_state.user_web_sessions[session->id] = session;
					world_state.markAsChanged();

					session_id = session->id;
				}
			}
		} // End lock scope

		// Send data back to client after releasing world state mutex.
		if(valid_username_and_credentials)
		{
			// Valid credentials.
			web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 302 Redirect" + CRLF);
			web::ResponseUtils::writeRawString(reply_info, "Location: " + return_URL + CRLF);
			web::ResponseUtils::writeRawString(reply_info, "Set-Cookie: site-b=" + session_id + "; Path=/; Max-Age=7776000; HttpOnly" + CRLF); // Max-Age is 90 days.
			// HttpOnly forbids JavaScript from accessing the cookie (https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Set-Cookie)
			web::ResponseUtils::writeRawString(reply_info, "Content-Length: 0" + CRLF); // NOTE: not sure if content-length is needed for 302 redirect.
			web::ResponseUtils::writeRawString(reply_info, CRLF);
		}
		else
		{
			// Invalid credentials or some other problem
			web::ResponseUtils::writeRedirectTo(reply_info, "/login?msg=" + web::Escaping::URLEscape("Invalid username or password, please try again."));
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("handleLoginPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}
	
	
void handleLogoutPost(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 302 Redirect" + CRLF);
	web::ResponseUtils::writeRawString(reply_info, "Location: /" + CRLF);
	web::ResponseUtils::writeRawString(reply_info, "Set-Cookie: site-b=; Path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT" + CRLF); // Clear cookie
	web::ResponseUtils::writeRawString(reply_info, "Content-Length: 0" + CRLF); // NOTE: not sure if content-length is needed for 302 redirect.
	web::ResponseUtils::writeRawString(reply_info, CRLF);
}


void renderSignUpPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Sign Up");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Sign Up</h1><body>";

	if(!msg.empty())
		page_out += "<div class=\"msg\" style=\"background-color: yellow\">" + msg.HTMLEscaped() + "</div>  \n";

	page_out += "<form action=\"signup_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"return\" value=\"" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\"><br>";
	page_out += "username: <input type=\"text\" name=\"username\"><br>";
	page_out += "email:    <input type=\"email\" name=\"email\"><br>";
	page_out += "password: <input type=\"password\" name=\"password\"><br/>";
	page_out += "<input type=\"submit\" value=\"Sign Up\">";
	page_out += "</form>";

	page_out += "<br/><br/><br/>";
	page_out += "<div>Don't have an account?  <a href=\"/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\">Sign up</a>";

	page_out += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


class InvalidCredentialsExcep : public glare::Exception
{
public:
	InvalidCredentialsExcep(const std::string& msg) : glare::Exception(msg) {}
};


void handleSignUpPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		const web::UnsafeString username		= request_info.getPostField("username");
		const web::UnsafeString email			= request_info.getPostField("email");
		const web::UnsafeString password		= request_info.getPostField("password");
		const web::UnsafeString raw_return_URL	= request_info.getPostField("return");

		/*conPrint("username:   '" + username.str() + "'");
		conPrint("email:      '" + email.str() + "'");
		conPrint("password:    '" + password.str() + "'");
		conPrint("raw_return_URL: '" + raw_return_URL.str() + "'");*/

		if(username.str().size() < 3)
			throw InvalidCredentialsExcep("Username is too short, must have at least 3 characters");
		if(password.str().size() < 6)
			throw InvalidCredentialsExcep("Password is too short, must have at least 6 characters");

		std::string return_URL = raw_return_URL.str();
		if(return_URL.empty())
			return_URL = "/";
		if(!isSafeReturnURL(return_URL))
			throw glare::Exception("Invalid return URL.");

		std::string reply;

		{ // Lock scope
			Lock lock(world_state.mutex);
			auto res = world_state.name_to_users.find(username.str()); // Find existing user with username
			if(res != world_state.name_to_users.end())
				throw InvalidCredentialsExcep("That username is not available."); // Username already used.
			
			Reference<User> new_user = new User();
			new_user->id = UserID((uint32)world_state.name_to_users.size());
			new_user->created_time = TimeStamp::currentTime();
			new_user->name = username.str();
			new_user->email_address = email.str();

			// We need a random salt for the user.
			uint8 random_bytes[32];
			CryptoRNG::getRandomBytes(random_bytes, 32);

			std::string user_salt;
			Base64::encode(random_bytes, 32, user_salt); // Convert random bytes to base-64.

			new_user->password_hash_salt = user_salt;
			new_user->hashed_password = User::computePasswordHash(password.str(), user_salt);

			// Add new user to world state
			world_state.user_id_to_users.insert(std::make_pair(new_user->id,   new_user));
			world_state.name_to_users   .insert(std::make_pair(username.str(), new_user));


			UserWebSessionRef session = new UserWebSession();
			session->id = UserWebSession::generateRandomKey();
			session->user_id = new_user->id;
			session->created_time = TimeStamp::currentTime();
			world_state.user_web_sessions[session->id] = session;

			world_state.markAsChanged(); // Mark as changed so gets saved to disk.

			reply += "HTTP/1.1 302 Redirect" + CRLF;
			reply += "Location: " + return_URL + CRLF;
			reply += "Set-Cookie: site-b=" + session->id + "; Path=/; Max-Age=7776000; HttpOnly" + CRLF; // Max-Age is 90 days.
			// HttpOnly forbids JavaScript from accessing the cookie (https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Set-Cookie)
			reply += "Content-Length: 0" + CRLF; // NOTE: not sure if content-length is needed for 302 redirect.
			reply += CRLF;
		} // End lock scope

		web::ResponseUtils::writeRawString(reply_info, reply);
	}
	catch(InvalidCredentialsExcep& e)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "&msg=" + web::Escaping::URLEscape(e.what()));
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSignUpPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace LoginHandlers
