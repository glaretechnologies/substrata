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

private slots:;
	void accepted();

private:
	QSettings* settings;
};
