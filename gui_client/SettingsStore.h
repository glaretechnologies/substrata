/*=====================================================================
SettingsStore.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <string>


/*=====================================================================
SettingsStore
-------------
Interface for a store for settings, like QSettings.
=====================================================================*/
class SettingsStore
{
public:
	virtual ~SettingsStore();

	virtual bool		getBoolValue(const std::string& key, bool default_value) = 0;
	virtual void		setBoolValue(const std::string& key, bool value) = 0;

	virtual int			getIntValue(const std::string& key, int default_value) = 0;
	virtual void		setIntValue(const std::string& key, int value) = 0;

	virtual double		getDoubleValue(const std::string& key, double default_value) = 0;
	virtual void		setDoubleValue(const std::string& key, double value) = 0;

	virtual std::string getStringValue(const std::string& key, const std::string& default_value) = 0;
	virtual void		setStringValue(const std::string& key, const std::string& value) = 0;
};
