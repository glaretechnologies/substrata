/*=====================================================================
WebServerResponseUtils.cpp
--------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "WebServerResponseUtils.h"


#include <ConPrint.h>
#include <Clock.h>
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
#include "Escaping.h"
#include "LoginHandlers.h"


namespace WebServerResponseUtils
{


const std::string CRLF = "\r\n";


const std::string standardHTMLHeader(const web::RequestInfo& request_info, const std::string& page_title)
{
	return
		"	<!DOCTYPE html>																								\n"
		"	<html>																									\n"
		"		<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">		\n"
		"		<title>" + web::Escaping::HTMLEscape(page_title) + "</title>																				\n"
		"		<style type=\"text/css\">																					\n"
		"		body																									\n"
		"		{																											\n"
		"			margin-top: 0;																							\n"
		"			margin-bottom: 40px;																							\n"
		"			margin-left: auto;																							\n"
		"			margin-right: auto;																							\n"
		"			max-width:650px;																						\n"
		"			line-height:1.6;																						\n"
		"			font-size:18px;																							\n"
		"			color:#222;																									\n"
		"			padding:0 10px;																								\n"
		"			font-family: Helvetica;																					\n"
		"		}																											\n"
		"		h1,h2,h3																								\n"
		"		{																											\n"
		"			line-height:1.2																							\n"
		"		}																											\n"
		"		#login																										\n"
		"		{																											\n"
		"			text-align: right;																											\n"
		"		}																											\n"
		"		</style>																								\n"
		"		</head>																									\n";
}


const std::string standardHeader(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, const std::string& page_title)
{
	std::string page_out = standardHTMLHeader(request_info, page_title);
	page_out +=
		"	<body>\n"
		"	<div id=\"login\">\n"; // Start login div
	
	web::UnsafeString logged_in_username;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username);

	if(logged_in)
	{
		page_out += "You are logged in as " + logged_in_username.HTMLEscaped();

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
	"	</div>																									\n" // End login div
	"	<header>																								\n"
	"		<h1>" + web::Escaping::HTMLEscape(page_title) + "</h1>												\n"
	"	</header>																								\n";
		
	return page_out;
}


const std::string standardFooter(const web::RequestInfo& request_info, bool include_email_link)
{
	std::string page_out;
	page_out +=
		"	<hr/>																						\n"
		"	Substrata is made by <a href=\"http://glaretechnologies.com\">Glare Technologies</a>.		\n"
		"	<br/>																						\n"
		"	Contact us at contact@glaretechnologies.com														\n"
		"																								\n"
		"	</body>																						\n"
		"</html>																						\n";

	return page_out;
}


} // end namespace WebServerResponseUtils
