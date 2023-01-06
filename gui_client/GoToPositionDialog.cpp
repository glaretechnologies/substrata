/*=====================================================================
GoToPositionDialog.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "GoToPositionDialog.h"


#include <QtCore/QSettings>
#include "../qt/SignalBlocker.h"


GoToPositionDialog::GoToPositionDialog(QSettings* settings_, const Vec3d& current_pos)
:	settings(settings_)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load window geometry and state
	this->restoreGeometry(settings->value("GoToPositionDialog/geometry").toByteArray());

	SignalBlocker::setValue(this->XDoubleSpinBox, current_pos.x);
	SignalBlocker::setValue(this->YDoubleSpinBox, current_pos.y);
	SignalBlocker::setValue(this->ZDoubleSpinBox, current_pos.z);
}


GoToPositionDialog::~GoToPositionDialog()
{
	settings->setValue("GoToPositionDialog/geometry", saveGeometry());
}
