/*=====================================================================
EnvironmentOptionsWidget.cpp
----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "EnvironmentOptionsWidget.h"


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>


EnvironmentOptionsWidget::EnvironmentOptionsWidget(QWidget* parent)
:	QWidget(parent),
	settings(NULL)
{
	setupUi(this);

	this->useLocalSunDirCheckBox->hide();

	this->sunThetaRealControl->setSliderSteps(200);

	connect(this->sunThetaRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
	connect(this->sunPhiRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
}


void EnvironmentOptionsWidget::init(QSettings* settings_) // settings should be set before this.
{
	settings = settings_;

	SignalBlocker::setValue(this->sunThetaRealControl, settings->value("environment_options/sun_theta", /*default val=*/40.0).toDouble());
	SignalBlocker::setValue(this->sunPhiRealControl,   settings->value("environment_options/sun_phi",   /*default val=*/0.0 ).toDouble());
}


EnvironmentOptionsWidget::~EnvironmentOptionsWidget()
{
}


void EnvironmentOptionsWidget::settingChangedSlot()
{
	if(settings)
	{
		settings->setValue("environment_options/sun_theta", this->sunThetaRealControl->value());
		settings->setValue("environment_options/sun_phi",   this->sunPhiRealControl->value());
	}

	emit settingChanged();
}
