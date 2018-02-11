/*=====================================================================
ResetPasswordDialog.h
---------------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "Ui_ResetPasswordDialog.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
ResetPasswordDialog
-------------------

=====================================================================*/
class ResetPasswordDialog : public QDialog, public Ui_ResetPasswordDialog
{
	Q_OBJECT
public:
	ResetPasswordDialog(QSettings* settings);
	~ResetPasswordDialog();


private slots:;
	void accepted();

private:
	QSettings* settings;
};
