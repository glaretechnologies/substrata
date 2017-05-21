/*=====================================================================
URLWidget.cpp
-------------
=====================================================================*/
#include "URLWidget.h"


#include "../qt/QtUtils.h"
#include "../qt/SignalBlocker.h"
#include "../utils/ConPrint.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <QtCore/QTimer>


URLWidget::URLWidget(QWidget* parent)
:	QWidget(parent),
	has_focus(false)
{
	setupUi(this);

	URLLineEdit->installEventFilter(this);

	connect(URLLineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressedSlot()));
}


URLWidget::~URLWidget()
{
}


void URLWidget::setURL(const std::string& new_URL)
{
	SignalBlocker b(this->URLLineEdit);
	this->URLLineEdit->setText(QtUtils::toQString(new_URL));
}


const std::string URLWidget::getURL() const
{
	return QtUtils::toIndString(this->URLLineEdit->text());
}


bool URLWidget::eventFilter(QObject *object, QEvent *event)
{
	if(event->type() == event->FocusIn && object == URLLineEdit)
	{
		has_focus = true;

		// URLLineEdit->selectAll(); // This doesn't seem to work being called directly, maybe because event hasn't been processed by line edit yet?
		//QTimer::singleShot(0, URLLineEdit, SLOT(selectAll()));
	}
	else if(event->type() == event->FocusOut && object == URLLineEdit)
	{
		has_focus = false;
	}

	return false; // Let the event continue to propagate
}


void URLWidget::returnPressedSlot()
{
	this->URLLineEdit->clearFocus();
	this->URLLineEdit->deselect();

	emit URLChanged();
}
