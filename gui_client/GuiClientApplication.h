/*=====================================================================
GuiClientApplication.h
----------------------
Copyright Glare Technologies Limited 2013 -
=====================================================================*/
#pragma once


#include <QtWidgets/QApplication>



/*=====================================================================
GuiClientApplication
--------------------
This subclass of QApplication is required for handling the
QFileOpenEvent, which is needed so opening files (for example by double
clicking in the Finder) works as expected.
=====================================================================*/
class GuiClientApplication : public QApplication
{
	Q_OBJECT

public:
	GuiClientApplication(int& argc, char** argv);
	
	QString getOpenFilename();
	void setOpenFilename(QString filename);

signals:;
	void openFileOSX(const QString);

protected:
	virtual bool event(QEvent * event);
	
private:
	QString filename;
};
