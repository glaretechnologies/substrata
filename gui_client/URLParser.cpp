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
	Parser parser(URL.c_str(), URL.size());

	// Parse protocol
	string_view protocol;
	parser.parseAlphaToken(protocol);
	if(protocol != "sub")
		throw Indigo::Exception("Unhandled protocol scheme '" + protocol + "'.");
	if(!parser.parseString("://"))
		throw Indigo::Exception("Expected '://' after protocol scheme.");

	// Parse hostname and userpath
	std::string hostname;
	std::string userpath;
	while(!parser.eof())
	{
		if(parser.current() == '/') // End of hostname, start of userpath if there is one.
		{
			parser.consume('/');

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

	double x = DEFAULT_X;
	double y = DEFAULT_Y;
	double z = DEFAULT_Z;
	if(parser.currentIsChar('?'))
	{
		parser.consume('?');

		if(!parser.parseChar('x'))
			throw Indigo::Exception("Expected 'x' after '?'.");

		if(!parser.parseChar('='))
			throw Indigo::Exception("Expected '=' after 'x'.");

		if(!parser.parseDouble(x))
			throw Indigo::Exception("Failed to parse x coord.");

		if(!parser.parseChar('&'))
			throw Indigo::Exception("Expected '&' after x coodinate.");

		if(!parser.parseChar('y'))
			throw Indigo::Exception("Expected 'y' after '?'.");

		if(!parser.parseChar('='))
			throw Indigo::Exception("Expected '=' after 'y'.");

		if(!parser.parseDouble(y))
			throw Indigo::Exception("Failed to parse y coord.");

		if(parser.currentIsChar('&'))
		{
			parser.consume('&');

			string_view URL_arg_name;
			if(!parser.parseToChar('=', URL_arg_name))
				throw Indigo::Exception("Failed to parse URL argument after &");
			if(URL_arg_name == "z")
			{
				if(!parser.parseChar('='))
					throw Indigo::Exception("Expected '=' after 'z'.");

				if(!parser.parseDouble(z))
					throw Indigo::Exception("Failed to parse z coord.");
			}
			else
				throw Indigo::Exception("Unknown URL arg '" + URL_arg_name.to_string() + "'");
		}

		conPrint("x: " + toString(x) + ", y: " + toString(y) + ", z: " + toString(z));
	}

	URLParseResults res;
	res.hostname = hostname;
	res.userpath = userpath;
	res.x = x;
	res.y = y;
	res.z = z;
	return res;
}


#if BUILD_TESTS


#include "../indigo/TestUtils.h"
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
	catch(Indigo::Exception&)
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

	testInvalidURL("ub://substrata.info");
	testInvalidURL("sub//substrata.info");
	testInvalidURL("sub/substrata.info");
	testInvalidURL("subsubstrata.info");
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
}


#endif // BUILD_TESTS
