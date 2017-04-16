/*=====================================================================
SignUpDialog.cpp
----------------
=====================================================================*/
#include "SignUpDialog.h"


#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>


SignUpDialog::SignUpDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("SignUpDialog/geometry").toByteArray());

	this->usernameLineEdit->setText(settings->value("SignUpDialog/username").toString());
	this->emailLineEdit   ->setText(settings->value("SignUpDialog/email"   ).toString());
	this->passwordLineEdit->setText(settings->value("SignUpDialog/password").toString()); // TODO: encrypt

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
	settings->setValue("SignUpDialog/password", this->passwordLineEdit->text());
}
