/*=====================================================================
ResourceProcessing.h
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <string>


/*=====================================================================
ResourceProcessing
------------------
Code for building resources/assets that ship with Substrata
=====================================================================*/
class ResourceProcessing
{
public:
	static void run(const std::string& appdata_path);
};
