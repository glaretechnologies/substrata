/*=====================================================================
TerrainSpecSectionWidget.h
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "ui_TerrainSpecSectionWidget.h"


class QSettings;


class TerrainSpecSectionWidget : public QWidget, public Ui::TerrainSpecSectionWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	TerrainSpecSectionWidget(QWidget* parent);
	~TerrainSpecSectionWidget();

	void updateControlsEditable(bool editable);
signals:
	void removeButtonClickedSignal();
};
