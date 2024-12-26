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
	static const QString SSAOKey() { return "setting/SSAO"; }

	static const QString BloomKey() { return "setting/bloom"; }

	static const QString limitFPSKey() { return "setting/limit_FPS"; }

	static const QString FPSLimitKey() { return "setting/FPS_limit"; }

	static const QString useCustomCacheDirKey() { return "setting/use_custom_cache_dir"; }

	static const QString customCacheDirKey() { return "setting/custom_cache_dir"; }
	
	static const QString startLocationURLKey() { return "setting/start_location_URL"; }

	static const QString inputDeviceNameKey() { return "setting/input_device_name"; }

	static const QString inputScaleFactorNameKey() { return "setting/input_scale_factor_name"; }

	static const QString showMinimapKey() { return "setting/show_minimap"; }

	static std::string getInputDeviceName(const QSettings* settings);
	static float getInputScaleFactor(const QSettings* settings);

	static bool getShowMinimap(const QSettings* settings);

private slots:;
	void accepted();
	void customCacheDirCheckBoxChanged(bool checked);

	void on_inputDeviceComboBox_currentIndexChanged(int index);
	void on_inputVolumeScaleHorizontalSlider_valueChanged(int new_value);

private:
	QSettings* settings;
};
