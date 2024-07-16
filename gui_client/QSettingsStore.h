/*=====================================================================
QSettingsStore.h
----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "SettingsStore.h"


class QSettings;


/*=====================================================================
QSettingsStore
--------------
Implements the SettingsStore interface using QSettings.
=====================================================================*/
class QSettingsStore final : public SettingsStore
{
public:
	QSettingsStore(QSettings* settings);
	virtual ~QSettingsStore();

	virtual bool		getBoolValue(const std::string& key, bool default_value) override;
	virtual void		setBoolValue(const std::string& key, bool value) override;

	virtual int			getIntValue(const std::string& key, int default_value) override;
	virtual void		setIntValue(const std::string& key, int value) override;

	virtual double		getDoubleValue(const std::string& key, double default_value) override;
	virtual void		setDoubleValue(const std::string& key, double value) override;

	virtual std::string getStringValue(const std::string& key, const std::string& default_value) override;
	virtual void		setStringValue(const std::string& key, const std::string& value) override;

private:
	QSettings* settings;
};
