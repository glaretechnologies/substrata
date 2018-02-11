/*=====================================================================
ResetPasswordDialog.h
---------------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "ui_ChangePasswordDialog.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
ResetPasswordDialog
-------------------

=====================================================================*/
class ChangePasswordDialog : public QDialog, public Ui_ChangePasswordDialog
{
	Q_OBJECT
public:
	ChangePasswordDialog(QSettings* settings);
	~ChangePasswordDialog();

	void setResetCodeLineEditVisible(bool v);

private slots:;
	void accepted();

private:
	QSettings* settings;
};
