/*=====================================================================
CameraControls.h
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Fri Jul 02 15:08:31 +1200 2010

This is a double spin box paired with a slider (which keep each other in synch).

If you use this class in QT designer, preferrably promote from a QDoubleSpinBox,
as this class implements most of its setMinimum, setMaximum etc functions (for ease of use).

If the slider should have a different range as the spin you have to call the setSliderMin/Max()
functions after setupUI. the slider will by default have the same range as the spin, with 10 steps.

=====================================================================*/
#pragma once


#include "ui_RealControl.h"


/*=====================================================================
RealControl
-------------

=====================================================================*/
class RealControl : public QWidget, public Ui_RealControl
{
	Q_OBJECT
public:
	/*=====================================================================
	RealControl
	-------------
	
	=====================================================================*/
	RealControl(QWidget* parent = 0);

	virtual ~RealControl();

	void initialize();

	void setUnitString(const QString& unit);
	//void setSpinRange(const double min, const double max, const double step = 1.0);
	//void setSliderRange(const double min, const double max, const double step = 1.0);
	void setValue(double v);
	double value();

	//NOTE: setDecimals has been disabled because we are using the new IndigoDoubleSpinBox that works best with the default number of decimal places.
	//void setDecimals(int d);
	void setMinimum(double min);
	void setMaximum(double max);
	void setSingleStep(double step);
	void setSuffix(const QString& suffix);

	void setSliderMinimum(double min);
	void setSliderMaximum(double max);
	void setSliderSteps(int steps);

signals:;
	void valueChanged(double v);

private slots:;
	void on_realSpin_valueChanged(double);
	void on_realSlider_valueChanged(int);

private:
	void updateSlider(double v);
	//double sliderPrecision; //controls the number of steps.
	double sliderMin, sliderMax;
};
