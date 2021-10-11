/*=====================================================================
Imposter.h
----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <string>


/*=====================================================================
Imposter
--------
=====================================================================*/
class Imposter
{
public:
	static void floodFillColourInTransparentRegions(const std::string& image_path_in, const std::string& image_path_out);
};
