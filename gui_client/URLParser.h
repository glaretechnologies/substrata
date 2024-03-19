/*=====================================================================
URLParser.h
-----------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include <string>


struct URLParseResults
{
	URLParseResults() : heading(90.0) {}

	std::string hostname;
	std::string userpath; // = worldname
	double x, y, z;
	double heading; // [0, 360].  0 = looking along x axis.  90 = looking along y axis
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
