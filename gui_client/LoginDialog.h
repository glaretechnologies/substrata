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

	static const std::string decryptPassword(const std::string& cyphertext);
	static const std::string encryptPassword(const std::string& password);

signals:
	void passWordResetRequested();

private slots:;
	void accepted();
	void on_resetPasswordLabel_linkActivated(const QString& link);

private:
	QSettings* settings;
};
