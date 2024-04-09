/*=====================================================================
TerrainSpecSectionWidget.cpp
----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainSpecSectionWidget.h"


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>


TerrainSpecSectionWidget::TerrainSpecSectionWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

	connect(this->removeTerrainSectionPushButton, SIGNAL(clicked()), this, SIGNAL(removeButtonClickedSignal()));
}


TerrainSpecSectionWidget::~TerrainSpecSectionWidget()
{}


void TerrainSpecSectionWidget::updateControlsEditable(bool editable)
{
	xSpinBox->setReadOnly(!editable);
	ySpinBox->setReadOnly(!editable);
	heightmapURLFileSelectWidget->setReadOnly(!editable);
	maskMapURLFileSelectWidget->setReadOnly(!editable);
	treeMaskMapURLFileSelectWidget->setReadOnly(!editable);
	removeTerrainSectionPushButton->setEnabled(editable);
}
