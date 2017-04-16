/*=====================================================================
UserDetailsWidget.h
-------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ui_UserDetailsWidget.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
UserDetailsWidget
-------------

=====================================================================*/
class UserDetailsWidget : public QWidget, public Ui_UserDetailsWidget
{
	Q_OBJECT
public:
	UserDetailsWidget(QWidget* parent);
	~UserDetailsWidget();

	void setTextAsNotLoggedIn();
	void setTextAsLoggedIn(const std::string& username);

signals:
	void logInClicked();
	void logOutClicked();
	void signUpClicked();

private slots:
	void on_userDetailsLabel_linkActivated(const QString& link);
};
