/*=====================================================================
ChangePasswordDialog.cpp
------------------------
=====================================================================*/
#include "ChangePasswordDialog.h"


#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <AESEncryption.h>
#include <Base64.h>
#include <Exception.h>
#include "../qt/QtUtils.h"


ChangePasswordDialog::ChangePasswordDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);


	// Load main window geometry and state
	this->restoreGeometry(settings->value("ChangePasswordDialog/geometry").toByteArray());

	//this->emailLineEdit->setText(settings->value("ResetPasswordDialog/email").toString());

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Change Password");

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
}


ChangePasswordDialog::~ChangePasswordDialog()
{
	settings->setValue("ChangePasswordDialog/geometry", saveGeometry());
}


void ChangePasswordDialog::setResetCodeLineEditVisible(bool v)
{
	this->resetCodeLabel->setVisible(v);
	this->resetCodeLineEdit->setVisible(v);
}


void ChangePasswordDialog::accepted()
{
	//settings->setValue("ResetPasswordDialog/email", this->emailLineEdit->text());
}