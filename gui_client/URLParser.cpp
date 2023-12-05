/*=====================================================================
URLParser.cpp
-------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "URLParser.h"


#include <utils/Parser.h>
#include <utils/Exception.h>
#include <utils/ConPrint.h>


static const double DEFAULT_X = 0;
static const double DEFAULT_Y = 0;
static const double DEFAULT_Z = 2;


URLParseResults URLParser::parseURL(const std::string& URL)
{
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

	// Parse hostname and userpath
	std::string hostname;
	std::string userpath;
	int parcel_index = -123;
	while(!parser.eof())
	{
		if(parser.current() == '/') // End of hostname, start of userpath if there is one.
		{
			parser.consume('/');

			if(parser.parseCString("parcel/"))
			{
				if(!parser.parseInt(parcel_index))
					throw glare::Exception("Failed to parse parcel number");
			}

			// Parse userpath, if present
			while(!parser.eof())
			{
				if(parser.current() == '?')
					break;
				else
				{
					userpath += parser.current();
					parser.advance();
				}
			}

			break;
		}
		else if(parser.current() == '?')
			break;

		hostname += parser.current();
		parser.advance();
	}

	URLParseResults res;
	res.parsed_x = false;
	res.parsed_y = false;
	res.parsed_z = false;
	res.parsed_parcel_uid = parcel_index != -123;
	res.parcel_uid = parcel_index;

	double x = DEFAULT_X;
	double y = DEFAULT_Y;
	double z = DEFAULT_Z;
	if(parser.currentIsChar('?'))
	{
		parser.consume('?');

		if(!parser.parseChar('x'))
			throw glare::Exception("Expected 'x' after '?'.");

		if(!parser.parseChar('='))
			throw glare::Exception("Expected '=' after 'x'.");

		if(!parser.parseDouble(x))
			throw glare::Exception("Failed to parse x coord.");
		res.parsed_x = true;

		if(!parser.parseChar('&'))
			throw glare::Exception("Expected '&' after x coodinate.");

		if(!parser.parseChar('y'))
			throw glare::Exception("Expected 'y' after '?'.");

		if(!parser.parseChar('='))
			throw glare::Exception("Expected '=' after 'y'.");

		if(!parser.parseDouble(y))
			throw glare::Exception("Failed to parse y coord.");
		res.parsed_y = true;

		if(parser.currentIsChar('&'))
		{
			parser.consume('&');

			string_view URL_arg_name;
			if(!parser.parseToChar('=', URL_arg_name))
				throw glare::Exception("Failed to parse URL argument after &");
			if(URL_arg_name == "z")
			{
				if(!parser.parseChar('='))
					throw glare::Exception("Expected '=' after 'z'.");

				if(!parser.parseDouble(z))
					throw glare::Exception("Failed to parse z coord.");
				res.parsed_z = true;
			}
			else
				throw glare::Exception("Unknown URL arg '" + toString(URL_arg_name) + "'");
		}

		conPrint("x: " + toString(x) + ", y: " + toString(y) + ", z: " + toString(z));
	}
	
	res.hostname = hostname;
	res.userpath = userpath;
	res.x = x;
	res.y = y;
	res.z = z;
	return res;
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
	{
		URLParseResults res = parseURL("sub://substrata.info");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "");
		testAssert(res.x == DEFAULT_X);
		testAssert(res.y == DEFAULT_Y);
		testAssert(res.z == DEFAULT_Z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "");
		testAssert(res.x == DEFAULT_X);
		testAssert(res.y == DEFAULT_Y);
		testAssert(res.z == DEFAULT_Z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "bleh");
		testAssert(res.x == DEFAULT_X);
		testAssert(res.y == DEFAULT_Y);
		testAssert(res.z == DEFAULT_Z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=1.0&y=2.0&z=3.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "bleh");
		testAssert(res.x == 1.0);
		testAssert(res.y == 2.0);
		testAssert(res.z == 3.0);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == DEFAULT_Z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.userpath == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == DEFAULT_Z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/10");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 10);
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
		testAssert(res.z == DEFAULT_Z);
	}

	testInvalidURL("ub://substrata.info");
	//testInvalidURL("sub//substrata.info"); // We handle missing ':' now.
	//testInvalidURL("sub/substrata.info");
	//testInvalidURL("subsubstrata.info");
	testInvalidURL("sub://substrata.info?");
	testInvalidURL("sub://substrata.info?x");
	testInvalidURL("sub://substrata.info?x=");
	testInvalidURL("sub://substrata.info?x=A");
	testInvalidURL("sub://substrata.info?x=1.0&");
	testInvalidURL("sub://substrata.info?x=1.0&y");
	testInvalidURL("sub://substrata.info?x=1.0&y=");
	testInvalidURL("sub://substrata.info?x=1.0&y=A");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0A"); // We'll allow garbage at the end for now.
	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&");
	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z");
	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=A");
	//testInvalidURL("sub://substrata.info/parcel");
	testInvalidURL("sub://substrata.info/parcel/");
	testInvalidURL("sub://substrata.info/parcel/a");
}


#endif // BUILD_TESTS
