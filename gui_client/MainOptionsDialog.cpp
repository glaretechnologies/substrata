/*=====================================================================
MainOptionsDialog.cpp
---------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MainOptionsDialog.h"


#include "../qt/QtUtils.h"
#include "../qt/SignalBlocker.h"
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>


MainOptionsDialog::MainOptionsDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	SignalBlocker::setValue(this->loadDistanceDoubleSpinBox, settings->value(objectLoadDistanceKey(), /*default val=*/500.0).toDouble());
}


MainOptionsDialog::~MainOptionsDialog()
{}


void MainOptionsDialog::accepted()
{
	settings->setValue(objectLoadDistanceKey(), this->loadDistanceDoubleSpinBox->value());
}
