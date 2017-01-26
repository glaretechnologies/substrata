/*=====================================================================
IndigoDoubleSpinBox.cpp
-------------------
Copyright Glare Technologies Limited 2012 -
Generated at Thu Nov 29 16:18:20 +0100 2012
=====================================================================*/
#include "IndigoDoubleSpinBox.h"


IndigoDoubleSpinBox::IndigoDoubleSpinBox(QWidget *parent)
	: QDoubleSpinBox(parent)
{
	this->setDecimals(12);
}


/*IndigoDoubleSpinBox::~IndigoDoubleSpinBox()
{

}*/


QString IndigoDoubleSpinBox::textFromValue(double val) const
{
	QString str = locale().toString(val, 'f', this->decimals());
	if (qAbs(val) >= 1000.0) {
		str.remove(locale().groupSeparator());
	}

	// Truncate trailing 0s, but not the last 0 before the decimal point.
	if (str.length() > 0) {
        int last_nonzero_idx = str.length() - 1;

        while (last_nonzero_idx > 0 && str.unicode()[last_nonzero_idx] == QLatin1Char('0'))
            --last_nonzero_idx;

		// Have we reached the decimal point? Don't take off the last 0.
		if(str.unicode()[last_nonzero_idx] == locale().decimalPoint())
			++last_nonzero_idx;

        str.truncate(last_nonzero_idx + 1);
    }

	return str;
}


#if defined(BUILD_TESTS)


#include "../../indigo/TestUtils.h"
#include "../../indigo/globals.h"
#include <qt/QtUtils.h>


static void compareResults(const QString result, const QString expected_result)
{
	if(result != expected_result)
		failTest(QtUtils::toIndString(QString(result + " != " + expected_result)));
	else
		conPrint(QtUtils::toIndString(QString(result + " == " + expected_result)));

}


void IndigoDoubleSpinBox::test()
{
	conPrint("Running IndigoDoubleSpinBox::test()");

	IndigoDoubleSpinBox* indigoSpin = new IndigoDoubleSpinBox(0);
	QString res;

	QString decimal_point = indigoSpin->locale().decimalPoint();

	compareResults(indigoSpin->textFromValue(1.0), "1" + decimal_point + "0");

	compareResults(indigoSpin->textFromValue(1000000000.0), "1000000000" + decimal_point + "0");

	compareResults(indigoSpin->textFromValue(0.000000001), "0" + decimal_point + "000000001");

	compareResults(indigoSpin->textFromValue(0.001), "0" + decimal_point + "001");

	compareResults(indigoSpin->textFromValue(1.1234567), "1" + decimal_point + "1234567");

	compareResults(indigoSpin->textFromValue(0.102030405), "0" + decimal_point + "102030405");
}


#endif

