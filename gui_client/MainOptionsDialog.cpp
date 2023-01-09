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

	connect(this->useCustomCacheDirCheckBox, SIGNAL(toggled(bool)), this, SLOT(customCacheDirCheckBoxChanged(bool)));

	this->customCacheDirFileSelectWidget->setSettingsKey("options/lastCacheDirFileSelectDir");
	this->customCacheDirFileSelectWidget->setType(FileSelectWidget::Type_Directory);

	const bool use_custom_cache_dir = settings->value(useCustomCacheDirKey(), /*default val=*/false).toBool();

	SignalBlocker::setValue(this->loadDistanceDoubleSpinBox,		settings->value(objectLoadDistanceKey(),	/*default val=*/500.0).toDouble());
	SignalBlocker::setChecked(this->shadowsCheckBox,				settings->value(shadowsKey(),				/*default val=*/true).toBool());
	SignalBlocker::setChecked(this->MSAACheckBox,					settings->value(MSAAKey(),					/*default val=*/true).toBool());
	SignalBlocker::setChecked(this->bloomCheckBox,					settings->value(BloomKey(),					/*default val=*/true).toBool());
	SignalBlocker::setChecked(this->useCustomCacheDirCheckBox,		use_custom_cache_dir);
	
	this->customCacheDirFileSelectWidget->setFilename(				settings->value(customCacheDirKey()).toString());

	this->customCacheDirFileSelectWidget->setEnabled(use_custom_cache_dir);

	this->startLocationURLLineEdit->setText(						settings->value(startLocationURLKey()).toString());
}


MainOptionsDialog::~MainOptionsDialog()
{}


void MainOptionsDialog::accepted()
{
	settings->setValue(objectLoadDistanceKey(),						this->loadDistanceDoubleSpinBox->value());
	settings->setValue(shadowsKey(),								this->shadowsCheckBox->isChecked());
	settings->setValue(MSAAKey(),									this->MSAACheckBox->isChecked());
	settings->setValue(BloomKey(),									this->bloomCheckBox->isChecked());
	settings->setValue(useCustomCacheDirKey(),						this->useCustomCacheDirCheckBox->isChecked());

	settings->setValue(customCacheDirKey(),							this->customCacheDirFileSelectWidget->filename());

	settings->setValue(startLocationURLKey(),						this->startLocationURLLineEdit->text());
}


void MainOptionsDialog::customCacheDirCheckBoxChanged(bool checked)
{
	this->customCacheDirFileSelectWidget->setEnabled(checked);
}
