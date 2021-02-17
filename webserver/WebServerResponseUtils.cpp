/*=====================================================================
WebServerResponseUtils.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-22 21:32:34 +0100
=====================================================================*/
#include "WebServerResponseUtils.h"


#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <Exception.h>
#include <mysocket.h>
#include <Lock.h>
#include <Clock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include "RequestInfo.h"
#include "Escaping.h"


namespace WebServerResponseUtils
{


const std::string CRLF = "\r\n";


const std::string standardHTMLHeader(const web::RequestInfo& request_info, const std::string& page_title)
{
	return
		"	<!DOCTYPE html>																								\n"
		"	<html>																									\n"
		"		<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">		\n"
		"		<title>Substrata</title>																				\n"
		"		<style type=\"text/css\">																					\n"
		"		body																									\n"
		"		{																											\n"
		"			margin:40px auto;																							\n"
		"			max-width:650px;																						\n"
		"			line-height:1.6;																						\n"
		"			font-size:18px;																							\n"
		"			color:#222;																									\n"
		"			padding:0 10px;																								\n"
		"			font-family: Helvetica;																					\n"
		"		}																											\n"
		"																												\n"
		"		h1,h2,h3																								\n"
		"		{																											\n"
		"			line-height:1.2																							\n"
		"		}																											\n"
		"		</style>																								\n"
		"		</head>																									\n"
		"		<body>																									\n"
		"		<header>																								\n"
		"		<h1>Substrata</h1>																						\n"
		"		</header>																								\n";
}


const std::string standardHeader(const web::RequestInfo& request_info, const std::string& page_title)
{
	std::string page_out = standardHTMLHeader(request_info, page_title);
	page_out += "<body>\n";
		
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
		"	</html>																						\n";

	return page_out;
}


} // end namespace WebServerResponseUtils
