/*=====================================================================
SignalBlocker.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at Tue Feb 26 14:29:40 +0100 2013
=====================================================================*/
#include "SignalBlocker.h"


#include "RealControl.h"
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAction>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QComboBox>
#include <assert.h>


void SignalBlocker::setChecked(QAbstractButton* btn, bool checked)
{
	assert(btn != NULL);
	SignalBlocker block(btn);
	btn->setChecked(checked);
}


void SignalBlocker::setChecked(QAction* action, bool checked)
{
	assert(action != NULL);
	SignalBlocker block(action);
	action->setChecked(checked);
}


void SignalBlocker::setValue(QSpinBox* spinbox, int val)
{
	assert(spinbox != NULL);
	SignalBlocker block(spinbox);
	spinbox->setValue(val);
}


void SignalBlocker::setValue(QDoubleSpinBox* spinbox, double val)
{
	assert(spinbox != NULL);
	SignalBlocker block(spinbox);
	spinbox->setValue(val);
}


void SignalBlocker::setValue(RealControl* realcontrol, double val)
{
	assert(realcontrol != NULL);
	SignalBlocker block(realcontrol);
	realcontrol->setValue(val);
}


void SignalBlocker::setCurrentIndex(QComboBox* combobox, int val)
{
	assert(combobox != NULL);
	SignalBlocker block(combobox);
	combobox->setCurrentIndex(val);
}
