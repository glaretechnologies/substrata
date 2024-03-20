/*=====================================================================
SDLSettingsStore.cpp
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "SDLSettingsStore.h"


#include <utils/PlatformUtils.h>
#include <utils/StringUtils.h>


SDLSettingsStore::~SDLSettingsStore()
{}

#if EMSCRIPTEN

 // TODO
bool SDLSettingsStore::getBoolValue(const std::string& key, bool default_value)
{
	return default_value;
}

void SDLSettingsStore::setBoolValue(const std::string& key, bool value)
{
}

int SDLSettingsStore::getIntValue(const std::string& key, int default_value)
{
	return default_value;
}

void SDLSettingsStore::setIntValue(const std::string& key, int value)
{
}

double SDLSettingsStore::getDoubleValue(const std::string& key, double default_value)
{
	return default_value;
}

void SDLSettingsStore::setDoubleValue(const std::string& key, double value)
{
}

std::string SDLSettingsStore::getStringValue(const std::string& key, const std::string& default_value)
{
	return default_value;
}

void SDLSettingsStore::setStringValue(const std::string& key, const std::string& value)
{
}

#else


static bool doesRegValueExist(const std::string& key)
{
	// TODO: improve
	try
	{
		std::vector<std::string> keyparts = ::split(key, '/');
		if(keyparts.size() == 0)
			throw glare::Exception("keyparts.size() == 0");

		std::string path = "SOFTWARE\\Glare Technologies\\Cyberspace\\";
		for(size_t i=0; i+1<keyparts.size(); ++i)
		{
			path += keyparts[i];
			if(i + 2 < keyparts.size())
				path += "\\";
		}
		std::string regvalue = keyparts.back();

		const std::string stringval = PlatformUtils::getStringRegKey(PlatformUtils::RegHKey_CurrentUser, path, regvalue);
		return true;
	}
	catch(glare::Exception&)
	{
		return false;
	}
}

static const std::string getRegStringVal(const std::string& key)
{
	std::vector<std::string> keyparts = ::split(key, '/');
	if(keyparts.size() == 0)
		throw glare::Exception("keyparts.size() == 0");

	std::string path = "SOFTWARE\\Glare Technologies\\Cyberspace\\";
	for(size_t i=0; i+1<keyparts.size(); ++i)
	{
		path += keyparts[i];
		if(i + 2 < keyparts.size())
			path += "\\";
	}
	std::string regvalue = keyparts.back();

	const std::string stringval = PlatformUtils::getStringRegKey(PlatformUtils::RegHKey_CurrentUser, path, regvalue);
	return stringval;
}

bool SDLSettingsStore::getBoolValue(const std::string& key, bool default_value)
{
	if(doesRegValueExist(key))
		return getRegStringVal(key) == "true";
	else
		return default_value;
}

void SDLSettingsStore::setBoolValue(const std::string& key, bool value)
{
}

int SDLSettingsStore::getIntValue(const std::string& key, int default_value)
{
	if(doesRegValueExist(key))
		return stringToInt(getRegStringVal(key));
	else
		return default_value;
}

void SDLSettingsStore::setIntValue(const std::string& key, int value)
{
}

double SDLSettingsStore::getDoubleValue(const std::string& key, double default_value)
{
	if(doesRegValueExist(key))
		return stringToDouble(getRegStringVal(key));
	else
		return default_value;
}

void SDLSettingsStore::setDoubleValue(const std::string& key, double value)
{
}

std::string SDLSettingsStore::getStringValue(const std::string& key, const std::string& default_value)
{
	if(doesRegValueExist(key))
		return getRegStringVal(key);
	else
		return default_value;
}

void SDLSettingsStore::setStringValue(const std::string& key, const std::string& value)
{
}


#endif