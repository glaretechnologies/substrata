/*=====================================================================
URLParser.h
-----------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include <string>


struct URLParseResults
{
	std::string hostname;
	std::string userpath;
	double x, y, z;
};


/*=====================================================================
URLParser
---------
=====================================================================*/
class URLParser
{
public:
	// Throws Indigo::Exception on parse error.
	static URLParseResults parseURL(const std::string& URL);

	static void test();
};
