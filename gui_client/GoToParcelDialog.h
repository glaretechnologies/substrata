/*=====================================================================
GoToParcelDialog.h
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ui_GoToParcelDialog.h"
#include <QtCore/QString>
class QSettings;


/*=====================================================================
GoToParcelDialog
----------------

=====================================================================*/
class GoToParcelDialog : public QDialog, public Ui_GoToParcelDialog
{
	Q_OBJECT
public:
	GoToParcelDialog(QSettings* settings);
	~GoToParcelDialog();

private:
	QSettings* settings;
};
