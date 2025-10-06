/*=====================================================================
WorldSettingsWidget.h
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/WorldSettings.h"
#include "TerrainSpecSectionWidget.h"
#include "ui_WorldSettingsWidget.h"
#include <vector>


class QSettings;
class MainWindow;


class WorldSettingsWidget : public QWidget, public Ui::WorldSettingsWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	WorldSettingsWidget(QWidget* parent);
	~WorldSettingsWidget();

	void init(MainWindow* main_window);

	void setFromWorldSettings(const WorldSettings& world_settings);

	void toWorldSettings(WorldSettings& world_settings_out);

	void updateControlsEditable();

signals:
	void settingsAppliedSignal();

protected slots:
	void newTerrainSectionPushButtonClicked();

	void removeTerrainSectionButtonClickedSlot();

	void applySettingsSlot();

private:
	URLString getURLForFileSelectWidget(FileSelectWidget* widget);

	//std::vector<TerrainSpecSectionWidget*> section_widgets;
	MainWindow* main_window;
};
