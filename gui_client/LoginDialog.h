/*=====================================================================
LoginDialog.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ui_LoginDialog.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
LoginDialog
-------------

=====================================================================*/
class LoginDialog : public QDialog, public Ui_LoginDialog
{
	Q_OBJECT
public:
	LoginDialog(QSettings* settings);
	~LoginDialog();

private slots:;
	void accepted();

private:
	QSettings* settings;
};
