/*=====================================================================
UserDetailsWidget.cpp
---------------------
=====================================================================*/
#include "UserDetailsWidget.h"


#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include "../utils/ConPrint.h"


UserDetailsWidget::UserDetailsWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

	this->setTextAsNotLoggedIn();
}


UserDetailsWidget::~UserDetailsWidget()
{

}


void UserDetailsWidget::setTextAsNotLoggedIn()
{
	this->userDetailsLabel->setText("<a href=\"#login\">Log in</a> or <a href=\"#signup\">Sign up</a>");
}


void UserDetailsWidget::setTextAsLoggedIn(const std::string& username)
{
	// TODO: escape name
	this->userDetailsLabel->setText("Logged in as " + QtUtils::toQString(username) + ".   <a href=\"#logout\">logout</a>");
}


void UserDetailsWidget::on_userDetailsLabel_linkActivated(const QString& link)
{
	if(link == "#login")
	{
		emit logInClicked();
	}
	else if(link == "#logout")
	{
		emit logOutClicked();
	}
	else if(link == "#signup")
	{
		emit signUpClicked();
	}
}
