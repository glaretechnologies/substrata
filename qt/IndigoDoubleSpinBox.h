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
Allows a lot of decimal places if needed, but only displays actually used
decimal places.
Note that this control works best if the 'decimals' property of QDoubleSpinBox
is not explicitly set in the designer but is left at the default (2).
=====================================================================*/
class IndigoDoubleSpinBox : public QDoubleSpinBox
{
	Q_OBJECT

public:
	explicit IndigoDoubleSpinBox(QWidget *parent = 0);
	//~IndigoDoubleSpinBox();

	virtual QSize	sizeHint() const;
	virtual QSize	minimumSizeHint() const;

	virtual QString textFromValue(double val) const;

	static void test();

private:

};



