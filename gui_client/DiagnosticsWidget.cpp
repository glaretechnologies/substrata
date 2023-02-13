/*=====================================================================
DiagnosticsWidget.cpp
---------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "DiagnosticsWidget.h"


#include <QtCore/QSettings>
#include <qt/QtUtils.h>
#include "../qt/SignalBlocker.h"
#include <StringUtils.h>


DiagnosticsWidget::DiagnosticsWidget(
	QWidget* parent
)
:	settings(NULL)
{
	setupUi(this);

	connect(this->showPhysicsObOwnershipCheckBox,	SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
}


DiagnosticsWidget::~DiagnosticsWidget()
{
}


void DiagnosticsWidget::init(QSettings* settings_)
{
	settings = settings_;
	SignalBlocker::setChecked(this->showPhysicsObOwnershipCheckBox, settings->value("diagnostics/show_physics_ob_ownership", false).toBool());
}


void DiagnosticsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("diagnostics/show_physics_ob_ownership", this->showPhysicsObOwnershipCheckBox->isChecked());
	}
}

