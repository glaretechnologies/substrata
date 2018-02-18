/*=====================================================================
ResetPasswordDialog.cpp
-----------------------
=====================================================================*/
#include "ResetPasswordDialog.h"


#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <AESEncryption.h>
#include <Base64.h>
#include <Exception.h>
#include "../qt/QtUtils.h"


ResetPasswordDialog::ResetPasswordDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("ResetPasswordDialog/geometry").toByteArray());

	this->emailLineEdit->setText(settings->value("ResetPasswordDialog/email").toString());

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Send Email");

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
}


ResetPasswordDialog::~ResetPasswordDialog()
{
	settings->setValue("ResetPasswordDialog/geometry", saveGeometry());
}


void ResetPasswordDialog::accepted()
{
	settings->setValue("ResetPasswordDialog/email", this->emailLineEdit->text());
}