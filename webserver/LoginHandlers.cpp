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


bool isLoggedIn(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out, bool& is_user_admin_out)
{
	Lock lock(world_state.mutex);

	const User* user = getLoggedInUser(world_state, request_info);
	if(user == NULL)
	{
		logged_in_username_out = "";
		is_user_admin_out = false;
		return false;
	}
	else
	{
		logged_in_username_out = user->name;
		is_user_admin_out = isGodUser(user->id);
		return true;
	}
}


bool loggedInUserHasAdminPrivs(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	web::UnsafeString logged_in_username;
	bool is_user_admin;
	const bool logged_in = isLoggedIn(world_state, request_info, logged_in_username, is_user_admin);
	return logged_in && is_user_admin;
}


// Returns NULL if not logged in as a valid user.
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


void setUserWebMessageForLoggedInUser(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, const std::string& message)
{
	Lock lock(world_state.mutex);
	User* user = getLoggedInUser(world_state, request_info);
	if(user)
	{
		world_state.setUserWebMessage(user->id, message);
	}
}


void renderLoginPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request_info, "Login to Substrata");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Log in</h1><body>";

	if(!msg.empty())
		page_out += "<div class=\"msg\">" + msg.HTMLEscaped() + "</div>  \n";

	page_out += "<form action=\"login_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"return\" value=\"" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\"><br/>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label for=\"username\">username</label><br/>";
	page_out += "<input id=\"username\"			autocomplete=\"username\"			required=\"required\"	type=\"text\"		name=\"username\">";
	page_out += "</div>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label for=\"current-password\">password</label><br/>";
	page_out += "<input id=\"current-password\"	autocomplete=\"current-password\"	required=\"required\"	type=\"password\"	name=\"password\">"; // See https://web.dev/sign-in-form-best-practices/#current-password
	page_out += "</div>";

	page_out += "<input type=\"submit\" value=\"Log in\">";
	page_out += "</form>";

	page_out += "<br/>";
	page_out += "<div><a href=\"/reset_password\">Forgot password?</a></div>";
	page_out += "<div>Don't have an account?  <a href=\"/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\">Sign up</a></div>";

	page_out += "</body></html>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		// Try and log in user
		const web::UnsafeString username = request_info.getPostField("username");
		const web::UnsafeString password = request_info.getPostField("password");
		const web::UnsafeString raw_return_URL = request_info.getPostField("return");

		//conPrint("username: '" + username.str() + "'");
		//conPrint("password: '" + password.str() + "'");
		//conPrint("raw_return_URL: '" + raw_return_URL.str() + "'");

		std::string return_URL = raw_return_URL.str();
		if(return_URL.empty())
			return_URL = "/";
		else
		{
			// Prefix the return URL with the current site hostname, to prevent redirects to dodgy sites.
			const std::string hostname = request_info.getHostHeader(); // Find the hostname the request was sent to
			if(hostname.empty())
				return_URL = "/";
			else
				return_URL = std::string(request_info.tls_connection ? "https://" : "http://") + hostname + return_URL;
		}

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
					
					world_state.addUserWebSessionAsDBDirty(session);

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
		if(!request_info.fuzzing)
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


void renderSignUpPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request_info, "Sign Up");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Sign Up</h1><body>";

	if(world_state.isInReadOnlyMode())
	{
		page_out += "<div class=\"info-notify\"><p>Server is in read-only mode, signing up is paused currently.</p></div>";
	}
	else
	{
		if(!msg.empty())
			page_out += "<div class=\"msg\">" + msg.HTMLEscaped() + "</div>  \n";

		page_out += "<form action=\"signup_post\" method=\"post\">";
		page_out += "<input type=\"hidden\" name=\"return\" value=\"" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\"><br>";

		page_out += "<div class=\"form-field\">";
		page_out += "<label for=\"username\">username</label><br/>";
		page_out += "<input id=\"username\"		autocomplete=\"username\"		required=\"required\"	type=\"text\"		name=\"username\">";
		page_out += "</div>";

		page_out += "<div class=\"form-field\">";
		page_out += "<label for=\"email\">email<label><br/>";
		page_out += "<input id=\"email\"			autocomplete=\"email\"		required=\"required\"	type=\"email\"		name=\"email\">";
		page_out += "</div>";

		page_out += "<div class=\"form-field\">";
		page_out += "<label for=\"new-password\">password</label><br/>";
		page_out += "<input id=\"new-password\"	autocomplete=\"new-password\"	required=\"required\"	type=\"password\"	name=\"password\">"; // See https://web.dev/sign-in-form-best-practices/#new-password
		page_out += "</div>";

		page_out += "<input type=\"submit\" value=\"Sign Up\">";
		page_out += "</form>";
	}

	page_out += "<br/><br/><br/>";

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
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, signups disabled currently.");
		if(!world_state.server_config.enable_registration)
			throw glare::Exception("Server is not currently accepting new registrations.");

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
		else
		{
			// Prefix the return URL with the current site hostname, to prevent redirects to dodgy sites.
			const std::string hostname = request_info.getHostHeader(); // Find the hostname the request was sent to
			if(hostname.empty())
				return_URL = "/";
			else
				return_URL = std::string(request_info.tls_connection ? "https://" : "http://") + hostname + return_URL;
		}

		std::string reply;

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);
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

			world_state.addUserAsDBDirty(new_user);

			world_state.addPersonalWorldForUser(new_user, lock);

			UserWebSessionRef session = new UserWebSession();
			session->id = UserWebSession::generateRandomKey();
			session->user_id = new_user->id;
			session->created_time = TimeStamp::currentTime();
			world_state.addUserWebSessionAsDBDirty(session);
			world_state.user_web_sessions[session->id] = session;

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
		if(!request_info.fuzzing)
			conPrint("handleSignUpPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderResetPasswordPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request_info, "Reset Password");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Reset Password</h1><body>";

	if(!msg.empty())
		page_out += "<div class=\"msg\">" + msg.HTMLEscaped() + "</div>  \n";

	page_out += "<form action=\"reset_password_post\" method=\"post\">";

	page_out += "<div class=\"form-field\">";
	page_out += "<label for=\"username\">Enter your email or username</label><br/>";
	page_out += "<input id=\"username\" autocomplete=\"username\" required=\"required\"	type=\"text\" name=\"username\">";
	page_out += "</div>";

	page_out += "<input type=\"submit\" value=\"Reset password\">";
	page_out += "</form>";

	page_out += "<br/><br/><br/>";

	page_out += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void handleResetPasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, password resets disabled currently.");

		const web::UnsafeString username_or_email = request_info.getPostField("username"); // or email address
		
		User* matching_user = NULL;
		if(StringUtils::containsChar(username_or_email.str(), '@'))
		{
			// Treat this as an email address
			const std::string email_addr = username_or_email.str();

			{ // Lock scope
				Lock lock(world_state.mutex);
				for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
					if(it->second->email_address == email_addr)
					{
						matching_user = it->second.getPointer();
						break;
					}
			} // End lock scope
		}
		else // Treat this as a username
		{
			const std::string username = username_or_email.str();

			{ // Lock scope
				Lock lock(world_state.mutex);
				for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
					if(it->second->name == username)
					{
						matching_user = it->second.getPointer();
						break;
					}
			} // End lock scope
		}

		if(matching_user)
		{
			// TEMP: Send password reset email in this thread for now. 
			// TODO: move to another thread (make some kind of background task?)
			try
			{
				EmailSendingInfo sending_info;
				sending_info.smtp_servername			= world_state.getCredential("email_sending_smtp_servername");
				sending_info.smtp_username				= world_state.getCredential("email_sending_smtp_username");
				sending_info.smtp_password				= world_state.getCredential("email_sending_smtp_password");
				sending_info.from_name					= world_state.getCredential("email_sending_from_name");
				sending_info.from_email_addr			= world_state.getCredential("email_sending_from_email_addr");
				sending_info.reset_webserver_hostname	= world_state.getCredential("email_sending_reset_webserver_hostname");

				matching_user->sendPasswordResetEmail(sending_info);

				Lock lock(world_state.mutex);
				world_state.addUserAsDBDirty(matching_user);
				
				conPrint("Sent user password reset email to '" + matching_user->email_address + ", username '" + matching_user->name + "'");
			}
			catch(glare::Exception& e)
			{
				conPrint("Sending password reset email failed: " + e.what());
			}

			web::ResponseUtils::writeRedirectTo(reply_info, "/reset_password?msg=" + web::Escaping::URLEscape("Email sent"));

			world_state.markAsChanged(); // Mark as changed so gets saved to disk.
		}
		else
		{
			web::ResponseUtils::writeRedirectTo(reply_info, "/reset_password?msg=" + web::Escaping::URLEscape("Could not find matching username or email address"));
		}
	}
	catch(InvalidCredentialsExcep& e)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "&msg=" + web::Escaping::URLEscape(e.what()));
	}
	catch(glare::Exception& e)
	{
		if(!request_info.fuzzing)
			conPrint("handleResetPasswordPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderResetPasswordFromEmailPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request_info, "Reset Password");

		const web::UnsafeString msg = request_info.getURLParam("msg");

		page_out += "<body>";
		page_out += "</head><h1>Reset Password</h1><body>";

		const std::string reset_token = request_info.getURLParam("token").str();

		const std::vector<unsigned char> token_hash_vec = SHA256::hash(reset_token);

		std::array<uint8, 32> token_hash;
		std::memcpy(token_hash.data(), token_hash_vec.data(), 32);


		if(!msg.empty())
			page_out += "<div class=\"msg\">" + msg.HTMLEscaped() + "</div>  \n";

		conPrint("reset_token: " + reset_token);
		//conPrint("new_password: " + new_password);

		bool valid_token = false;
		{
			Lock lock(world_state.mutex);

			// Find user with the given email address:
			for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
			{
				User* user = it->second.getPointer();
				if(user->isResetTokenHashValidForUser(token_hash))
				{
					valid_token = true;
					break;
				}
			}
		}

		if(valid_token)
		{
			page_out += "<form action=\"set_new_password_post\" method=\"post\">";
			page_out += "Enter a new password: <input type=\"password\" name=\"password\"><br>";
			page_out += "<input type=\"hidden\" name=\"reset_token\" value=\"" + web::Escaping::HTMLEscape(reset_token) + "\"><br>";
			page_out += "<input type=\"submit\" value=\"Set new password\">";
			page_out += "</form>";

			page_out += "<br/><br/><br/>";
		}
		else
		{
			page_out += "Sorry, that reset token is invalid.  It may have been used, or it may have expired.";
		}

		page_out += WebServerResponseUtils::standardFooter(request_info, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
	}
	catch(InvalidCredentialsExcep& e)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "&msg=" + web::Escaping::URLEscape(e.what()));
	}
	catch(glare::Exception& e)
	{
		if(!request_info.fuzzing)
			conPrint("renderResetPasswordFromEmailPage error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// From the reset password link from the password reset email
void handleSetNewPasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, password resetting disabled currently.");

		const std::string reset_token = request_info.getPostField("reset_token").str();
		const std::string new_password = request_info.getPostField("password").str();

		const std::vector<unsigned char> token_hash_vec = SHA256::hash(reset_token);

		std::array<uint8, 32> token_hash;
		std::memcpy(token_hash.data(), token_hash_vec.data(), 32);

		if(new_password.size() < 6)
		{
			web::ResponseUtils::writeRedirectTo(reply_info, "/reset_password_email?token=" + web::Escaping::URLEscape(reset_token) + "&msg=" + web::Escaping::URLEscape("Password is too short, must have at least 6 characters"));
			return;
		}

		bool password_reset = false;
		{
			Lock lock(world_state.mutex);

			// Find user with the given email address:
			for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
			{
				User* user = it->second.getPointer();
				if(user->isResetTokenHashValidForUser(token_hash))
				{
					password_reset = user->resetPasswordWithTokenHash(token_hash, new_password);
					if(password_reset)
					{
						world_state.addUserAsDBDirty(user);
						break;
					}
				}
			}
		}

		if(password_reset)
		{
			if(!request_info.fuzzing)
				conPrint("handleSetNewPasswordPost(): User succesfully reset password.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/login?msg=" + web::Escaping::URLEscape("New password set"));
		}
		else
		{
			if(!request_info.fuzzing)
				conPrint("handleSetNewPasswordPost(): User failed to reset password.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/login?msg=" + web::Escaping::URLEscape("Failed to set new password"));
		}
	}
	catch(glare::Exception& e)
	{
		if(!request_info.fuzzing)
			conPrint("handleSetNewPasswordPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderChangePasswordPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request_info, "Change Password");

		page_out += "<body>";
		page_out += "</head><h1>Change Password</h1><body>";

		// Display any messages for the user
		{ // lock scope
			Lock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
			if(logged_in_user)
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page_out += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}
			else
				throw glare::Exception("Must be logged in to change password");
		}

		page_out += "<form action=\"change_password_post\" method=\"post\">";

		page_out += "<div class=\"form-field\">";
		page_out += "<label for=\"current-password\">Enter current password:</label><br/>";
		page_out += "<input id=\"current-password\"	required=\"required\"	type=\"password\"	autocomplete=\"current-password\"	name=\"current_password\">";
		page_out += "</div>";

		page_out += "<div class=\"form-field\">";
		page_out += "<label for=\"new-password\">Enter a new password:</label><br/>";
		page_out += "<input id=\"new-password\"		required=\"required\"	type=\"password\"	autocomplete=\"new-password\"		name=\"new_password\">";
		page_out += "</div>";

		page_out += "<input type=\"submit\" value=\"Set new password\">";
		page_out += "</form>";

		page_out += "<br/><br/><br/>";

		page_out += WebServerResponseUtils::standardFooter(request_info, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleChangePasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, password changing disabled currently.");

		const std::string current_password = request_info.getPostField("current_password").str();
		const std::string new_password = request_info.getPostField("new_password").str();

		if(new_password.size() < 6)
		{
			setUserWebMessageForLoggedInUser(world_state, request_info, "Password is too short, must have at least 6 characters.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/change_password");
			return;
		}

		bool password_changed = false;
		{
			Lock lock(world_state.mutex);

			User* user = getLoggedInUser(world_state, request_info);
			if(!user)
				throw glare::Exception("Must be logged in");

			if(user->isPasswordValid(current_password))
			{
				user->setNewPasswordAndSalt(new_password);
				world_state.addUserAsDBDirty(user);
				password_changed = true;
			}
		}

		if(password_changed)
		{
			if(!request_info.fuzzing)
				conPrint("handleChangePasswordPost(): User succesfully changed password.");
			setUserWebMessageForLoggedInUser(world_state, request_info, "New password set.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
		}
		else
		{
			if(!request_info.fuzzing)
				conPrint("handleChangePasswordPost(): User failed to changed password.");
			setUserWebMessageForLoggedInUser(world_state, request_info, "Failed to set new password.  Please check you entered current password correctly.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/change_password");
		}
	}
	catch(glare::Exception& e)
	{
		if(!request_info.fuzzing)
			conPrint("handleChangePasswordPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace LoginHandlers
