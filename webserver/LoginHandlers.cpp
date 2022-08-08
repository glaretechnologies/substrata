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
	page_out += "<input type=\"submit\" value=\"Log in\">";
	page_out += "</form>";

	page_out += "<br/>";
	page_out += "<div><a href=\"/reset_password\">Forgot password?</a></div>";
	page_out += "<div>Don't have an account?  <a href=\"/signup?return=" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\">Sign up</a></div>";

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

		//conPrint("username: '" + username.str() + "'");
		//conPrint("password: '" + password.str() + "'");
		//conPrint("raw_return_URL: '" + raw_return_URL.str() + "'");

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
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Sign Up");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Sign Up</h1><body>";

	if(world_state.isInReadOnlyMode())
	{
		page_out += "<div style=\"background-color: blanchedalmond;\"><p>Server is in read-only mode, signing up is paused currently.</p></div>";
	}
	else
	{
		if(!msg.empty())
			page_out += "<div class=\"msg\" style=\"background-color: yellow\">" + msg.HTMLEscaped() + "</div>  \n";

		page_out += "<form action=\"signup_post\" method=\"post\">";
		page_out += "<input type=\"hidden\" name=\"return\" value=\"" + web::Escaping::HTMLEscape(request_info.getURLParam("return").str()) + "\"><br>";
		page_out += "username: <input type=\"text\" name=\"username\"><br>";
		page_out += "email:    <input type=\"email\" name=\"email\"><br>";
		page_out += "password: <input type=\"password\" name=\"password\"><br/>";
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

			world_state.addUserAsDBDirty(new_user);


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
		conPrint("handleSignUpPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderResetPasswordPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Reset Password");

	const web::UnsafeString msg = request_info.getURLParam("msg");

	page_out += "<body>";
	page_out += "</head><h1>Reset Password</h1><body>";

	if(!msg.empty())
		page_out += "<div class=\"msg\" style=\"background-color: yellow\">" + msg.HTMLEscaped() + "</div>  \n";

	page_out += "<form action=\"reset_password_post\" method=\"post\">";
	page_out += "Enter your email or username: <input type=\"text\" name=\"username\"><br>";
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
				matching_user->sendPasswordResetEmail(world_state.server_credentials);

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
		conPrint("handleSignUpPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderResetPasswordFromEmailPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Reset Password");

		const web::UnsafeString msg = request_info.getURLParam("msg");

		page_out += "<body>";
		page_out += "</head><h1>Reset Password</h1><body>";

		const std::string reset_token = request_info.getURLParam("token").str();

		const std::vector<unsigned char> token_hash_vec = SHA256::hash(reset_token);

		std::array<uint8, 32> token_hash;
		std::memcpy(token_hash.data(), token_hash_vec.data(), 32);


		if(!msg.empty())
			page_out += "<div class=\"msg\" style=\"background-color: yellow\">" + msg.HTMLEscaped() + "</div>  \n";

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
		conPrint("handleSignUpPost error: " + e.what());
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
			conPrint("handleSetNewPasswordPost(): User succesfully reset password.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/login?msg=" + web::Escaping::URLEscape("New password set"));
		}
		else
		{
			conPrint("handleSetNewPasswordPost(): User failed to reset password.");
			web::ResponseUtils::writeRedirectTo(reply_info, "/login?msg=" + web::Escaping::URLEscape("Failed to set new password"));
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSignUpPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace LoginHandlers
