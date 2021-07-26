/*=====================================================================
FindObjectDialog.h
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ui_FindObjectDialog.h"
#include <QtCore/QString>
class QSettings;


/*=====================================================================
FindObjectDialog
----------------

=====================================================================*/
class FindObjectDialog : public QDialog, public Ui_FindObjectDialog
{
	Q_OBJECT
public:
	FindObjectDialog(QSettings* settings);
	~FindObjectDialog();

private:
	QSettings* settings;
};
