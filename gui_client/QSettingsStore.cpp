/*=====================================================================
QSettingsStore.cpp
------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "QSettingsStore.h"


#include "../qt/QtUtils.h"
#include <QtCore/QSettings>


QSettingsStore::QSettingsStore(QSettings* settings_)
:	settings(settings_)
{}


QSettingsStore::~QSettingsStore()
{}


bool QSettingsStore::getBoolValue(const std::string& key, bool default_value)
{
	return settings->value(QtUtils::toQString(key), default_value).toBool();
}


void QSettingsStore::setBoolValue(const std::string& key, bool value)
{
	settings->setValue(QtUtils::toQString(key), value);
}


int	QSettingsStore::getIntValue(const std::string& key, int default_value)
{
	return settings->value(QtUtils::toQString(key), default_value).toInt();
}


void QSettingsStore::setIntValue(const std::string& key, int value)
{
	settings->setValue(QtUtils::toQString(key), value);
}


double QSettingsStore::getDoubleValue(const std::string& key, double default_value)
{
	return settings->value(QtUtils::toQString(key), default_value).toDouble();
}


void QSettingsStore::setDoubleValue(const std::string& key, double value)
{
	settings->setValue(QtUtils::toQString(key), value);
}


std::string QSettingsStore::getStringValue(const std::string& key, const std::string& default_value)
{
	return QtUtils::toStdString(settings->value(QtUtils::toQString(key), QtUtils::toQString(default_value)).toString());
}


void QSettingsStore::setStringValue(const std::string& key, const std::string& value)
{
	settings->setValue(QtUtils::toQString(key), QtUtils::toQString(value));
}
