/*=====================================================================
SignUpDialog.cpp
----------------
=====================================================================*/
#include "SignUpDialog.h"


#include "CredentialManager.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>


SignUpDialog::SignUpDialog(QSettings* settings_, CredentialManager* credential_manager_, const std::string& server_hostname_)
:	settings(settings_),
	server_hostname(server_hostname_),
	credential_manager(credential_manager_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);


	// Load main window geometry and state
	this->restoreGeometry(settings->value("SignUpDialog/geometry").toByteArray());

	this->usernameLineEdit->setText(settings->value("SignUpDialog/username").toString());
	this->emailLineEdit   ->setText(settings->value("SignUpDialog/email"   ).toString());
	//this->passwordLineEdit->setText(settings->value("SignUpDialog/password").toString());

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Sign up");

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
}


SignUpDialog::~SignUpDialog()
{
	settings->setValue("SignUpDialog/geometry", saveGeometry());
}


void SignUpDialog::accepted()
{
	settings->setValue("SignUpDialog/username", this->usernameLineEdit->text());
	settings->setValue("SignUpDialog/email",    this->emailLineEdit->text());
	//settings->setValue("SignUpDialog/password", this->passwordLineEdit->text());

	// Save these credentials as well, so that next time the user starts Substrata it can log them in.
	credential_manager->setDomainCredentials(server_hostname, QtUtils::toStdString(this->usernameLineEdit->text()), QtUtils::toStdString(this->passwordLineEdit->text()));

	credential_manager->saveToSettings(*settings);
}
