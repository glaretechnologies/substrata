/*=====================================================================
MainOptionsDialog.h
-------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ui_MainOptionsDialog.h"
#include <QtCore/QString>
#include <string>
class QSettings;
class CredentialManager;


/*=====================================================================
MainOptionsDialog
-----------------

=====================================================================*/
class MainOptionsDialog : public QDialog, private Ui_MainOptionsDialog
{
	Q_OBJECT
public:
	// server_hostname is the hostname of the currently-connected server (empty string if not connected), used for
	// editing the MCP API key for that server.
	MainOptionsDialog(QSettings* settings_, CredentialManager& credential_manager_, const std::string& server_hostname_, bool only_load_most_important_obs_default);

	~MainOptionsDialog();

	// Strings associated with registry keys.
	static const QString objectLoadDistanceKey() { return "ob_load_distance"; }
	static const QString onlyLoadMostImportantObsKey() { return "only_load_most_important_obs"; }

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

	static const QString darkModeKey() { return "setting/dark_mode"; }

	// NOTE: these MCP keys are also read in MainWindow::startMCPClientServerIfEnabled().
	// The MCP API key is stored per-server-domain in CredentialManager, not in a settings key.
	static const QString MCPEnabledKey()	{ return "mcp_client/enabled"; }
	static const QString MCPPortKey()		{ return "mcp_client/port"; }

	static int defaultMCPPort() { return 8095; }

	static std::string getInputDeviceName(const QSettings* settings);
	static float getInputScaleFactor(const QSettings* settings);

	static bool getShowMinimap(const QSettings* settings);

private slots:;
	void accepted();
	void customCacheDirCheckBoxChanged(bool checked);
	void MCPCheckBoxChanged(bool checked);

	void on_inputDeviceComboBox_currentIndexChanged(int index);
	void on_inputVolumeScaleHorizontalSlider_valueChanged(int new_value);

private:
	QSettings* settings;
	CredentialManager& credential_manager;
	std::string server_hostname; // Hostname of the currently-connected server, or empty string if not connected.
};
