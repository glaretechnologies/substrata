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
	static bool initialisationFailed();
	static std::string getInitialisationFailureErrorString();

	static void initialiseCEF(const std::string& base_dir_path, const std::string& appdata_path);

	static void shutdownCEF();

	static LifeSpanHandler* getLifespanHandler();

	static void doMessageLoopWork();
};
