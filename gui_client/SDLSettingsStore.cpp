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


static void getRegSubKeyAndValueName(const std::string& setting_key, std::string& subkey_out, std::string& value_name_out)
{
	std::vector<std::string> keyparts = ::split(setting_key, '/');
	if(keyparts.size() == 0)
		throw glare::Exception("setting_key.size() == 0");

	subkey_out = "SOFTWARE\\Glare Technologies\\Cyberspace\\";
	for(size_t i=0; i+1<keyparts.size(); ++i)
	{
		subkey_out += keyparts[i];
		if(i + 2 < keyparts.size())
			subkey_out += "\\";
	}
	value_name_out = keyparts.back();
}


static bool doesRegValueExist(const std::string& setting_key)
{
	// TODO: improve
	try
	{
		std::string subkey, value_name;
		getRegSubKeyAndValueName(setting_key, subkey, value_name);

		const std::string stringval = PlatformUtils::getStringRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name);
		return true;
	}
	catch(glare::Exception&)
	{
		return false;
	}
}


static const std::string getRegStringVal(const std::string& setting_key)
{
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);

	const std::string stringval = PlatformUtils::getStringRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name);
	return stringval;

}


static uint32 getRegDWordVal(const std::string& setting_key)
{
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);
	return PlatformUtils::getDWordRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name);
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
		return (int)getRegDWordVal(key);
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