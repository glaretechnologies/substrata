/*=====================================================================
MainOptionsDialog.h
-------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ui_MainOptionsDialog.h"
#include <QtCore/QString>
class QSettings;


/*=====================================================================
MainOptionsDialog
-----------------

=====================================================================*/
class MainOptionsDialog : public QDialog, private Ui_MainOptionsDialog
{
	Q_OBJECT
public:
	MainOptionsDialog(QSettings* settings_);

	~MainOptionsDialog();

	// Strings associated with registry keys.
	static const QString objectLoadDistanceKey() { return "ob_load_distance"; }

	static const QString shadowsKey() { return "setting/shadows"; }

	static const QString MSAAKey() { return "setting/MSAA"; }

	static const QString BloomKey() { return "setting/bloom"; }

	static const QString limitFPSKey() { return "setting/limit_FPS"; }

	static const QString FPSLimitKey() { return "setting/FPS_limit"; }

	static const QString useCustomCacheDirKey() { return "setting/use_custom_cache_dir"; }

	static const QString customCacheDirKey() { return "setting/custom_cache_dir"; }
	
	static const QString startLocationURLKey() { return "setting/start_location_URL"; }

private slots:;
	void accepted();
	void customCacheDirCheckBoxChanged(bool checked);

private:
	QSettings* settings;
};
