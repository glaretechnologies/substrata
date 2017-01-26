/*=====================================================================
SignalBlocker.h
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at Tue Feb 26 14:29:40 +0100 2013
=====================================================================*/
#pragma once


#include <QtCore/QObject>


class QAbstractButton;
class QAction;
class QSpinBox;
class RealControl;
class QComboBox;
class QDoubleSpinBox;


/*=====================================================================
SignalBlocker
-------------------
RAII object that blocks signals on QObjects.
=====================================================================*/
class SignalBlocker
{
public:
	SignalBlocker( QObject *obj )
		: m_obj( obj )
	{
		m_old = m_obj != NULL ? obj->blockSignals(true) : false;
	}

	~SignalBlocker()
	{
		if(m_obj) m_obj->blockSignals( m_old );
	}

	static void setChecked(QAbstractButton* btn, bool checked); // QCheckBox is derived from QAbstractButton.
	static void setChecked(QAction* action, bool checked);

	static void setValue(QSpinBox* spinbox, int val);
	static void setValue(QDoubleSpinBox* spinbox, double val);
	static void setValue(RealControl* spinbox, double val);
	
	static void setCurrentIndex(QComboBox* combobox, int val);

private:
	QObject *m_obj;
	bool m_old;
};
