/*=====================================================================
DiagnosticsWidget.h
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "ui_DiagnosticsWidget.h"
#include <string>

class QSettings;


/*=====================================================================
DiagnosticsWidget
-----------------

=====================================================================*/
class DiagnosticsWidget : public QWidget, public Ui_DiagnosticsWidget
{
	Q_OBJECT
public:
	DiagnosticsWidget(QWidget* parent);
	~DiagnosticsWidget();

	void init(QSettings* settings);

protected slots:
	void settingsChanged();
private:
	QSettings* settings;
};
