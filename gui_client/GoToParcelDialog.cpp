/*=====================================================================
GoToParcelDialog.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "GoToParcelDialog.h"


#include <QtCore/QSettings>


GoToParcelDialog::GoToParcelDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load window geometry and state
	this->restoreGeometry(settings->value("GoToParcelDialog/geometry").toByteArray());
}


GoToParcelDialog::~GoToParcelDialog()
{
	settings->setValue("GoToParcelDialog/geometry", saveGeometry());
}
