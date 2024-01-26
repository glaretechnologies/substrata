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
class CredentialManager;


/*=====================================================================
LoginDialog
-------------

=====================================================================*/
class LoginDialog : public QDialog, public Ui_LoginDialog
{
	Q_OBJECT
public:
	LoginDialog(QSettings* settings, CredentialManager& credential_manager, const std::string& server_hostname);
	~LoginDialog();

	

private slots:;
	void accepted();

private:
	std::string server_hostname;
	QSettings* settings;
};
