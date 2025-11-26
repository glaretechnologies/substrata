/*=====================================================================
URLParser.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "URLParser.h"


#include <networking/URL.h>
#include <utils/Parser.h>
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <webserver/Escaping.h>


URLParseResults::URLParseResults()
:	x(0), y(0), z(0),
	heading(90.0),
	parcel_uid(0),
	sun_vert_angle(45),
	sun_azimuth_angle(45),
	parsed_sun_vert_angle(false),
	parsed_sun_azimuth_angle(false),
	parsed_x(false),
	parsed_y(false),
	parsed_z(false),
	parsed_heading(false),
	parsed_parcel_uid(false)
{}


URLParseResults URLParser::parseURL(const std::string& URL)
{
	URLParseResults res;

	Parser parser(URL);

	// Parse protocol
	if(URL.find_first_of(':') != std::string::npos) // Allow user to skip protocol prefix: If there is no ':' character in string, assume protocol prefix is missing and skip it.
	{
		string_view protocol;
		parser.parseAlphaToken(protocol);
		if(protocol != "sub")
			throw glare::Exception("Unhandled protocol scheme '" + toString(protocol) + "'.");
		if(!parser.parseString("://"))
			throw glare::Exception("Expected '://' after protocol scheme.");
	}

	// Parse hostname and worldname, parcel number
	while(!parser.eof())
	{
		if(parser.current() == '/') // End of hostname, start of worldname if there is one.
		{
			parser.consume('/');

			if(parser.parseCString("parcel/"))
			{
				if(!parser.parseInt(res.parcel_uid))
					throw glare::Exception("Failed to parse parcel number");
				res.parsed_parcel_uid = true;
			}

			// Parse worldname, if present
			while(parser.notEOF() && parser.current() != '?')
			{
				res.worldname.push_back(parser.current());
				parser.advance();
			}

			res.worldname = web::Escaping::URLUnescape(res.worldname);

			break;
		}
		else if(parser.current() == '?')
			break;

		res.hostname += parser.current();
		parser.advance();
	}

	if(parser.currentIsChar('?'))
	{
		parser.consume('?');

		const std::string query_string = URL.substr(parser.currentPos());
		std::map<std::string, std::string> query_keyvalues = URL::parseQuery(query_string);

		processQueryKeyValues(query_keyvalues, res);
	}
	
	return res;
}


void URLParser::processQueryKeyValues(const std::map<std::string, std::string>& query_keyvalues, URLParseResults& res)
{
	if(query_keyvalues.count("x"))
	{
		res.x = stringToDouble(query_keyvalues.find("x")->second);
		res.parsed_x = true;
	}
	if(query_keyvalues.count("y"))
	{
		res.y = stringToDouble(query_keyvalues.find("y")->second);
		res.parsed_y = true;
	}
	if(query_keyvalues.count("z"))
	{
		res.z = stringToDouble(query_keyvalues.find("z")->second);
		res.parsed_z = true;
	}
	
	if(query_keyvalues.count("world"))
		res.worldname = query_keyvalues.find("world")->second; // An alternative way of specifying the world/user name

	if(query_keyvalues.count("heading"))
	{
		res.heading = stringToDouble(query_keyvalues.find("heading")->second);
		res.parsed_heading = true;
	}

	if(query_keyvalues.count("sun_vert_angle"))
	{
		res.sun_vert_angle = stringToDouble(query_keyvalues.find("sun_vert_angle")->second);
		res.parsed_sun_vert_angle = true;
	}

	if(query_keyvalues.count("sun_azimuth_angle"))
	{
		res.sun_azimuth_angle = stringToDouble(query_keyvalues.find("sun_azimuth_angle")->second);
		res.parsed_sun_azimuth_angle = true;
	}
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../maths/PCG32.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"


static void testInvalidURL(const std::string& URL)
{
	try
	{
		URLParser::parseURL(URL);
		failTest("Exception expected.");
	}
	catch(glare::Exception&)
	{}
}


void URLParser::test()
{
	//----------------------------------- Test processQueryKeyValues by itself -----------------------------------
	{
		std::map<std::string, std::string> query_keyvalues;
		URLParseResults res;
		processQueryKeyValues(query_keyvalues, res);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		std::map<std::string, std::string> query_keyvalues;
		query_keyvalues["x"] = "1.0";
		query_keyvalues["z"] = "3.0";
		URLParseResults res;
		processQueryKeyValues(query_keyvalues, res);
		testAssert(res.parsed_x);
		testAssert(res.x == 1.0);
		testAssert(!res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(res.z == 3.0);
		testAssert(!res.parsed_parcel_uid);
	}

	//----------------------------------- Test parseURL -----------------------------------
	{
		URLParseResults res = parseURL("sub://substrata.info");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my+cool+world");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my cool world");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my%3Fcool%3Fworld");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my?cool?world");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name and position params
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my%3Fcool%3Fworld?x=1.0&y=2.0&z=3.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my?cool?world");
		testAssert(res.x == 1.0);
		testAssert(res.y == 2.0);
		testAssert(res.z == 3.0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=1.0&y=2.0&z=3.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == 1.0);
		testAssert(res.y == 2.0);
		testAssert(res.z == 3.0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/10");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 10);
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/102343");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 102343);
	}

	// Test parcel URL with coords as well. not sure this should be supported.
	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/102343?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 102343);
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
	}

	testInvalidURL("ub://substrata.info");
	//testInvalidURL("sub//substrata.info"); // We handle missing ':' now.
	//testInvalidURL("sub/substrata.info");
	//testInvalidURL("subsubstrata.info");
//	testInvalidURL("sub://substrata.info?");
//	testInvalidURL("sub://substrata.info?x");
	testInvalidURL("sub://substrata.info?x=");
	testInvalidURL("sub://substrata.info?x=A");
//	testInvalidURL("sub://substrata.info?x=1.0&");
	//testInvalidURL("sub://substrata.info?x=1.0&y");
	testInvalidURL("sub://substrata.info?x=1.0&y=");
	testInvalidURL("sub://substrata.info?x=1.0&y=A");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0A"); // We'll allow garbage at the end for now.
//	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&");
//	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z");
	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=A");
	//testInvalidURL("sub://substrata.info/parcel");
	testInvalidURL("sub://substrata.info/parcel/");
	testInvalidURL("sub://substrata.info/parcel/a");
}


#endif // BUILD_TESTS
