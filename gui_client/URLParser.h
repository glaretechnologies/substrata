/*=====================================================================
URLParser.h
-----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <string>
#include <map>


struct URLParseResults
{
	URLParseResults();

	std::string hostname;
	std::string userpath; // = worldname
	double x, y, z;
	double heading; // [0, 360].  0 = looking along x axis.  90 = looking along y axis
	int parcel_uid;

	double sun_vert_angle; // degrees, default 45
	double sun_azimuth_angle; // degrees, default 45
	bool parsed_sun_vert_angle, parsed_sun_azimuth_angle;

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

	static void processQueryKeyValues(const std::map<std::string, std::string>& query_keyvalues, URLParseResults& parse_results_in_out);

	static void test();
};
