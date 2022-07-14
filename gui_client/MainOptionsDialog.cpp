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
	SignalBlocker::setChecked(this->shadowsCheckBox, settings->value(shadowsKey(), /*default val=*/true).toBool());
	SignalBlocker::setChecked(this->MSAACheckBox, settings->value(MSAAKey(), /*default val=*/true).toBool());
	SignalBlocker::setChecked(this->bloomCheckBox, settings->value(BloomKey(), /*default val=*/true).toBool());
}


MainOptionsDialog::~MainOptionsDialog()
{}


void MainOptionsDialog::accepted()
{
	settings->setValue(objectLoadDistanceKey(), this->loadDistanceDoubleSpinBox->value());
	settings->setValue(shadowsKey(), this->shadowsCheckBox->isChecked());
	settings->setValue(MSAAKey(), this->MSAACheckBox->isChecked());
	settings->setValue(BloomKey(), this->bloomCheckBox->isChecked());
}
