/*=====================================================================
RealControl.h
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Fri Jul 02 15:08:31 +1200 2010
=====================================================================*/

#include "RealControl.h"

#include <math.h>
#include "../../indigo/globals.h"


RealControl::RealControl(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

	sliderMin = 0;
	sliderMax = 1;

	realSlider->setMinimum(0);
	realSlider->setMaximum(100);
	realSlider->setSingleStep(1);

	realSpin->setMinimum(0);
	realSpin->setMaximum(1);
	realSpin->setSingleStep(0.1);
}


RealControl::~RealControl()
{

}


void RealControl::initialize()
{

}


void RealControl::setUnitString(const QString& unit)
{
	this->realSpin->setSuffix(unit);
}


//void RealControl::setSpinRange(const double min, const double max, const double step)
//{
//	this->realSpin->setMinimum(min);
//
//	this->realSpin->setMaximum(max);
//
//	this->realSpin->setSingleStep(step);
//}


//void RealControl::setDecimals(int d)
//{
//	this->realSpin->setDecimals(d);
//}


void RealControl::setMinimum(double min)
{
	this->realSpin->setMinimum(min);
	sliderMin = min;
	//this->realSlider->setMinimum((int)(min * sliderPrecision));
}


void RealControl::setMaximum(double max)
{
	this->realSpin->setMaximum(max);
	sliderMax = max;
	//this->realSlider->setMaximum((int)(max * sliderPrecision));
}


void RealControl::setSingleStep(double step)
{
	this->realSpin->setSingleStep(step);
	//this->realSlider->setSingleStep(std::max<double>(sliderPrecision * step, 1));
}


void RealControl::setSliderMinimum(double min)
{
	sliderMin = min;
}


void RealControl::setSliderMaximum(double max)
{
	sliderMax = max;
}


void RealControl::setSliderSteps(int steps)
{
	realSlider->setMinimum(0);
	realSlider->setMaximum(steps);
}


void RealControl::setReadOnly(bool readonly)
{
	this->realSlider->setEnabled(!readonly); // QSlider doesn't have setReadOnly
	this->realSpin->setReadOnly(readonly);
}


void RealControl::setSuffix(const QString& suffix)
{
	this->realSpin->setSuffix(suffix);
}


void RealControl::setValue(double v)
{
	updateSlider(v);

	this->realSpin->setValue(v);
}


double RealControl::value()
{
	return this->realSpin->value();
}


double lerp(double a, double b, double t)
{
	return a * (1.0 - t) + b * t;
}


void RealControl::updateSlider(double v)
{
	realSlider->blockSignals(true);
	if(v < sliderMin) realSlider->setValue(realSlider->minimum());
	else if(v > sliderMax) realSlider->setValue(realSlider->maximum());
	else
	{
		double sliderRange = fabs(sliderMin - sliderMax);
		double absValue = fabs(sliderMin - v);
		realSlider->setValue(lerp(realSlider->minimum(), realSlider->maximum(), absValue / sliderRange));
	}
	realSlider->blockSignals(false);
}


void RealControl::on_realSpin_valueChanged(double v)
{
	updateSlider(v);

	emit valueChanged(realSpin->value());
}


void RealControl::on_realSlider_valueChanged(int v)
{
	this->realSpin->blockSignals(true);
	this->realSpin->setValue(lerp(sliderMin, sliderMax, double(v) / realSlider->maximum()));
	this->realSpin->blockSignals(false);

	emit valueChanged(realSpin->value());
}
