/*=====================================================================
URLWhitelist.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <vector>
#include <string>


/*=====================================================================
URLWhitelist
------------
A whitelist of prefixes of allowed URLs
=====================================================================*/
class URLWhitelist
{
public:
	URLWhitelist();

	void loadDefaultWhitelist();

	bool isURLPrefixInWhitelist(const std::string& URL) const;

	static void test();

	std::vector<std::string> prefixes; // Should have no protocol scheme, should have a at least a slash for a path (important!), not just a raw domain
private:
};
