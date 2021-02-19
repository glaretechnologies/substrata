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
#include <MemMappedFile.h>
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "../server/ServerWorldState.h"


namespace LoginHandlers
{


const std::string CRLF = "\r\n";


const std::string cookie_AES_key  = "AtyU5$fG9HJeRtJgtReDyy";
const std::string cookie_AES_salt = "B6e64H&5";



/*
Cookies
-------
site-a cookie currently stores encrypted username.
*/


bool isLoggedIn(const web::RequestInfo& request_info, web::UnsafeString& logged_in_username_out)
{
	for(size_t i=0; i<request_info.cookies.size(); ++i)
	{
		if(request_info.cookies[i].key == "site-a")
		{
			try
			{
				// Decrypt cookie
				AESEncryption enc(cookie_AES_key, cookie_AES_salt);
				const std::string cyphertext_hex = request_info.cookies[i].value;

				const std::vector<unsigned char> cyphertext = StringUtils::convertHexToBinary(cyphertext_hex);
				const std::vector<unsigned char> plaintext_bytes = enc.decrypt(cyphertext);
				const std::string plaintext = StringUtils::byteArrayToString(plaintext_bytes);

				logged_in_username_out = plaintext;
				return true;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error: " + e.what());
			}
			catch(StringUtilsExcep& e)
			{
				conPrint("Error: " + e.what());
			}
		}
	}

	logged_in_username_out = "";
	return false;
}


void renderLoginPage(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHTMLHeader(request_info, "Substrata");

	page_out += "<body>";
	page_out += "</head><h1>Log in</h1><body>";
	page_out += "<form action=\"login_post\" method=\"post\">";
	page_out += "username: <input type=\"text\" name=\"username\"><br>";
	page_out += "password: <input type=\"text\" name=\"password\"><br/>";
	page_out += "<input type=\"submit\" value=\"Submit\">";
	page_out += "</form>";
	page_out += "</body></html>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void handleLoginPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	// Try and log in user
	if(request_info.post_fields.size() == 2)
	{
		if(request_info.post_fields[0].key == "username" && request_info.post_fields[1].key == "password")
		{
			const web::UnsafeString& username = request_info.post_fields[0].value;
			const web::UnsafeString& password = request_info.post_fields[1].value;
			conPrint("username: '" + username.str() + "'");
			conPrint("password: '" + password.str() + "'");

			bool valid_username_and_credentials = false;
			{ // Lock scope

				Lock lock(world_state.mutex);

				// Lookup by username
				const auto res = world_state.name_to_users.find(username.str());
				if(res != world_state.name_to_users.end())
				{
					// Found user for username
					const User& user = *(res->second);
					if(user.isPasswordValid(password.str()))
					{
						valid_username_and_credentials = true;
					}
				}
			} // End lock scope

			// Send data back to client after releasing world state mutex.
			if(valid_username_and_credentials)
			{
				// Valid credentials.
				conPrint("Valid credentials, logging in!");
				web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 302 Redirect" + CRLF);
				web::ResponseUtils::writeRawString(reply_info, "Location: /" + CRLF);

				// Compute encrpyted login cookie
				AESEncryption enc(cookie_AES_key, cookie_AES_salt);
				const std::vector<unsigned char> cyphertext = enc.encrypt(StringUtils::stringToByteArray(username.str()));
				const std::string cyphertext_hex = StringUtils::convertByteArrayToHexString(cyphertext.data(), cyphertext.size());

				web::ResponseUtils::writeRawString(reply_info, "Set-Cookie: site-a=" + cyphertext_hex + "; Path=/" + CRLF);
				web::ResponseUtils::writeRawString(reply_info, "Content-Length: 0" + CRLF); // NOTE: not sure if content-length is needed for 302 redirect.
				web::ResponseUtils::writeRawString(reply_info, CRLF);
				return;
			}
		}
	}

	// Invalid credentials or some other problem
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Invalid credentials.");
	conPrint("Invalid credentials.");
	//web::ResponseUtils::writeRedirectTo(reply_info, "/");
}
	
	
void handleLogoutPost(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	conPrint("handleLogoutPost");

	web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 302 Redirect" + CRLF);
	web::ResponseUtils::writeRawString(reply_info, "Location: /" + CRLF);
	web::ResponseUtils::writeRawString(reply_info, "Set-Cookie: site-a=; Path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT" + CRLF); // Clear cookie
	web::ResponseUtils::writeRawString(reply_info, "Content-Length: 0" + CRLF); // NOTE: not sure if content-length is needed for 302 redirect.
	web::ResponseUtils::writeRawString(reply_info, CRLF);
}


} // end namespace LoginHandlers
