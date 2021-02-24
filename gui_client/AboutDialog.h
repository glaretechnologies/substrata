/*=====================================================================
AboutDialog.h
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at Fri Apr 05 15:18:57 +0200 2013
=====================================================================*/
#pragma once


#include "ui_AboutDialog.h"


/*=====================================================================
AboutDialog
-------------------

=====================================================================*/
class AboutDialog : public QDialog, public Ui_AboutDialog
{
	Q_OBJECT
public:
	AboutDialog(QWidget* parent, const std::string& appdata_path);
	~AboutDialog();

private slots:;
	void on_generateCrashLabel_linkActivated(const QString& link);
};
