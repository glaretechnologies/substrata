/*=====================================================================
CEF.h
-----
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <string>


class LifeSpanHandler;


/*=====================================================================
CEF
---
Chromium Embedded Framework code.
=====================================================================*/
class CEF
{ 
public:
	static std::string CEFVersionString();

	static bool isInitialised();

	static void initialiseCEF(const std::string& base_dir_path);

	static void shutdownCEF();

	static LifeSpanHandler* getLifespanHandler();

	static void doMessageLoopWork();
};
