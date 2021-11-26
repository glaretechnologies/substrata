/*=====================================================================
URLWidget.h
-----------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "ui_URLWidget.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
URLWidget
---------
The URL widget / address bar, where a user can enter a URL or location, and 
see the current URL / location.
=====================================================================*/
class URLWidget : public QWidget, public Ui_URLWidget
{
	Q_OBJECT
public:
	URLWidget(QWidget* parent);
	~URLWidget();

	void setURL(const std::string& new_URL);
	const std::string getURL() const;

	// When the user clicks in the URL lineedit, or selects some of the text, the URL shouldn't be updated.
	// Selecting some of the text requires no updates because it allows the user to right-click and copy the URL.
	bool shouldBeUpdated();

	bool eventFilter(QObject *object, QEvent *event);

private slots:
	void returnPressedSlot();

signals:
	void URLChanged();

private:
	bool has_focus;
};
