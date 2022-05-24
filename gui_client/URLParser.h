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
	int parcel_uid;

	bool parsed_x;
	bool parsed_y;
	bool parsed_z;
	bool parsed_parcel_uid;
};


/*=====================================================================
URLParser
---------
Parse a Substrata URL
=====================================================================*/
class URLParser
{
public:
	// Throws glare::Exception on parse error.
	static URLParseResults parseURL(const std::string& URL);

	static void test();
};
