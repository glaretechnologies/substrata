/*=====================================================================
IndigoDoubleSpinBox.h
-------------------
Copyright Glare Technologies Limited 2012 -
Generated at Thu Nov 29 16:18:20 +0100 2012
=====================================================================*/
#pragma once


#include <QtWidgets/QDoubleSpinBox>


/*=====================================================================
IndigoDoubleSpinBox
-------------------

=====================================================================*/
class IndigoDoubleSpinBox : public QDoubleSpinBox
{
	Q_OBJECT

public:
	explicit IndigoDoubleSpinBox(QWidget *parent = 0);
	//~IndigoDoubleSpinBox();

	virtual QString textFromValue(double val) const;

	static void test();

private:

};



