/*=====================================================================
SDLSettingsStore.cpp
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "SDLSettingsStore.h"


#include <utils/PlatformUtils.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#if EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#include <string>
#endif


SDLSettingsStore::~SDLSettingsStore()
{}


#if EMSCRIPTEN

// When running under Emscripten, e.g. in the web browser, we will store the settings in localStorage.  See https://developer.mozilla.org/en-US/docs/Web/API/Web_Storage_API


// Define getLocalStorageKeyValue(const char* key) function
EM_JS(char*, getLocalStorageKeyValue, (const char* key), {
	const res = localStorage.getItem(UTF8ToString(key));
	if(res == null)
		return null;
	else
		return stringToNewUTF8(res);
});


// Define setLocalStorageKeyValue(const char* key, const char* value) function
EM_JS(void, setLocalStorageKeyValue, (const char* key, const char* value), {
	localStorage.setItem(UTF8ToString(key), UTF8ToString(value));
});


bool SDLSettingsStore::getBoolValue(const std::string& key, bool default_value)
{
	try
	{
		char* val = getLocalStorageKeyValue(key.c_str());
		if(val == NULL)
			return default_value;
		else
		{
			const bool bool_val = stringEqual(val, "true");
			free(val);
			return bool_val;
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get bool setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setBoolValue(const std::string& key, bool value)
{
	setLocalStorageKeyValue(key.c_str(), value ? "true" : "false");
}


int SDLSettingsStore::getIntValue(const std::string& key, int default_value)
{
	try
	{
		char* val = getLocalStorageKeyValue(key.c_str());
		if(val == NULL)
			return default_value;
		else
		{
			const int int_val = stringToInt(std::string(val));
			free(val);
			return int_val;
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get int setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setIntValue(const std::string& key, int value)
{
	setLocalStorageKeyValue(key.c_str(), toString(value).c_str());
}


double SDLSettingsStore::getDoubleValue(const std::string& key, double default_value)
{
	try
	{
		char* val = getLocalStorageKeyValue(key.c_str());
		if(val == NULL)
			return default_value;
		else
		{
			const double double_val = stringToDouble(std::string(val));
			free(val);
			return double_val;
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get double setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setDoubleValue(const std::string& key, double value)
{
	setLocalStorageKeyValue(key.c_str(), doubleToString(value).c_str());
}


std::string SDLSettingsStore::getStringValue(const std::string& key, const std::string& default_value)
{
	try
	{
		char* val = getLocalStorageKeyValue(key.c_str());
		if(val == NULL)
			return default_value;
		else
		{
			const std::string string_val(val);
			free(val);
			return string_val;
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get sting setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setStringValue(const std::string& key, const std::string& value)
{
	setLocalStorageKeyValue(key.c_str(), value.c_str());
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
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);
	return PlatformUtils::doesRegKeyAndValueExist(PlatformUtils::RegHKey_CurrentUser, subkey, value_name);
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
	try
	{
		if(doesRegValueExist(key))
			return getRegStringVal(key) == "true";
		else
			return default_value;
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get bool setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setBoolValue(const std::string& setting_key, bool value)
{
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);
	PlatformUtils::setStringRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name, /*new valuedata=*/value ? "true" : "false");
}


int SDLSettingsStore::getIntValue(const std::string& key, int default_value)
{
	try
	{
		if(doesRegValueExist(key))
			return (int)getRegDWordVal(key);
		else
			return default_value;
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get int setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setIntValue(const std::string& key, int value)
{
	// TODO
}


double SDLSettingsStore::getDoubleValue(const std::string& key, double default_value)
{
	try
	{
		if(doesRegValueExist(key))
			return stringToDouble(getRegStringVal(key));
		else
			return default_value;
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get double setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setDoubleValue(const std::string& setting_key, double value)
{
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);

	const std::string value_string = ::doubleToString(value);
	PlatformUtils::setStringRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name, /*new valuedata=*/value_string);
}


std::string SDLSettingsStore::getStringValue(const std::string& key, const std::string& default_value)
{
	try
	{
		if(doesRegValueExist(key))
			return getRegStringVal(key);
		else
			return default_value;
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: failed to get string setting: " + e.what());
		return default_value;
	}
}


void SDLSettingsStore::setStringValue(const std::string& setting_key, const std::string& value)
{
	std::string subkey, value_name;
	getRegSubKeyAndValueName(setting_key, subkey, value_name);
	PlatformUtils::setStringRegKey(PlatformUtils::RegHKey_CurrentUser, subkey, value_name, /*new valuedata=*/value);
}


#endif