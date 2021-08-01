/*=====================================================================
LoginDialog.cpp
---------------
=====================================================================*/
#include "LoginDialog.h"


#include "CredentialManager.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <AESEncryption.h>
#include <Base64.h>
#include <Exception.h>
#include "../qt/QtUtils.h"


LoginDialog::LoginDialog(QSettings* settings_, const std::string& server_hostname_)
:	settings(settings_),
	server_hostname(server_hostname_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("LoginDialog/geometry").toByteArray());


	CredentialManager manager;
	manager.loadFromSettings(*settings);

	this->usernameLineEdit->setText(QtUtils::toQString(manager.getUsernameForDomain(server_hostname)));
	this->passwordLineEdit->setText(QtUtils::toQString(manager.getDecryptedPasswordForDomain(server_hostname)));

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Log in");

	this->resetPasswordLabel->setOpenExternalLinks(true);
	this->resetPasswordLabel->setText("<a href=\"https://substrata.info/reset_password\">Forgot password?</a>");

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
}


LoginDialog::~LoginDialog()
{
	settings->setValue("LoginDialog/geometry", saveGeometry());
}


void LoginDialog::accepted()
{
	CredentialManager manager;
	manager.loadFromSettings(*settings);

	manager.setDomainCredentials(server_hostname, QtUtils::toStdString(this->usernameLineEdit->text()), QtUtils::toStdString(this->passwordLineEdit->text()));

	manager.saveToSettings(*settings);

	settings->setValue("LoginDialog/auto_login", true);
}


