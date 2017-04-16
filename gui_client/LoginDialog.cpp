/*=====================================================================
LoginDialog.cpp
---------------
=====================================================================*/
#include "LoginDialog.h"


#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>


LoginDialog::LoginDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("LoginDialog/geometry").toByteArray());

	this->usernameLineEdit->setText(settings->value("LoginDialog/username").toString());
	this->passwordLineEdit->setText(settings->value("LoginDialog/password").toString()); // TODO: encrypt

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Log in");

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
}


LoginDialog::~LoginDialog()
{
	settings->setValue("LoginDialog/geometry", saveGeometry());
}


void LoginDialog::accepted()
{
	settings->setValue("LoginDialog/username", this->usernameLineEdit->text());
	settings->setValue("LoginDialog/password", this->passwordLineEdit->text());
}
