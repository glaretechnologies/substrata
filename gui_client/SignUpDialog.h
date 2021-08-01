/*=====================================================================
SignUpDialog.h
--------------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "ui_SignUpDialog.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
SignUpDialog
------------

=====================================================================*/
class SignUpDialog : public QDialog, public Ui_SignUpDialog
{
	Q_OBJECT
public:
	SignUpDialog(QSettings* settings, const std::string& server_hostname);
	~SignUpDialog();

private slots:;
	void accepted();

private:
	std::string server_hostname;
	QSettings* settings;
};
