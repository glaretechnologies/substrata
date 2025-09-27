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

	connect(this->showFrameTimeGraphsCheckBox,		SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->showPhysicsObOwnershipCheckBox,	SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->showVehiclePhysicsVisCheckBox,	SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->showWireframesCheckBox,			SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->showLodChunkVisCheckBox,			SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->graphicsDiagnosticsCheckBox,		SIGNAL(toggled(bool)),	this, SLOT(settingsChanged()));
	connect(this->reloadTerrainPushButton,			SIGNAL(clicked()),		this, SIGNAL(reloadTerrainSignal()));
}


DiagnosticsWidget::~DiagnosticsWidget()
{
}


void DiagnosticsWidget::init(QSettings* settings_)
{
	settings = settings_;
	SignalBlocker::setChecked(this->showFrameTimeGraphsCheckBox, settings->value("diagnostics/show_frame_time_graphs", false).toBool());
	SignalBlocker::setChecked(this->showPhysicsObOwnershipCheckBox, settings->value("diagnostics/show_physics_ob_ownership", false).toBool());
	SignalBlocker::setChecked(this->showVehiclePhysicsVisCheckBox, settings->value("diagnostics/show_vehicle_physics_vis", false).toBool());
	SignalBlocker::setChecked(this->graphicsDiagnosticsCheckBox, settings->value("diagnostics/show_graphics_diagnostics", false).toBool());
}


void DiagnosticsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("diagnostics/show_frame_time_graphs", this->showFrameTimeGraphsCheckBox->isChecked());
		settings->setValue("diagnostics/show_physics_ob_ownership", this->showPhysicsObOwnershipCheckBox->isChecked());
		settings->setValue("diagnostics/show_vehicle_physics_vis", this->showVehiclePhysicsVisCheckBox->isChecked());
		settings->setValue("diagnostics/show_graphics_diagnostics", this->graphicsDiagnosticsCheckBox->isChecked());
	}

	emit settingsChangedSignal();
}
