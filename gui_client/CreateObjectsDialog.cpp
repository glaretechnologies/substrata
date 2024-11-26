/*=====================================================================
CreateObjectsDialog.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "CreateObjectsDialog.h"


#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include <utils/ConPrint.h>
#include <QtCore/QSettings>
#include <QtWidgets/QPushButton>


CreateObjectsDialog::CreateObjectsDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load window geometry and state
	this->restoreGeometry(settings->value("CreateObjectsDialog/geometry").toByteArray());

	buttonBox->button(QDialogButtonBox::StandardButton::Ok)->setEnabled(false);

	startTimer(100);
}


CreateObjectsDialog::~CreateObjectsDialog()
{
	settings->setValue("CreateObjectsDialog/geometry", saveGeometry());
}


void CreateObjectsDialog::timerEvent(QTimerEvent* event)
{
	while(!msg_queue->empty())
	{
		const std::string msg = msg_queue->dequeue();

		if(msg == "Done.")
		{
			buttonBox->button(QDialogButtonBox::StandardButton::Ok    )->setEnabled(true);
			buttonBox->button(QDialogButtonBox::StandardButton::Cancel)->setEnabled(false);
		}

		outputPlainTextEdit->appendPlainText(QtUtils::toQString(msg));
	}
}
