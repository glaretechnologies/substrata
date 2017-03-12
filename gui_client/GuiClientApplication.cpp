/*=====================================================================
GuiClientApplication.cpp
---------------------
Copyright Glare Technologies Limited 2013 -
Generated at Tue May 05 17:30:00 +0200 2013
=====================================================================*/
#include "GuiClientApplication.h"


#include "../utils/ConPrint.h"
#include <QtGui/QFileOpenEvent>
#include <QtWidgets/QMessageBox>



GuiClientApplication::GuiClientApplication(int& argc, char** argv)
:	QApplication(argc, argv)
{

}


QString GuiClientApplication::getOpenFilename()
{
	return filename;
}


void GuiClientApplication::setOpenFilename(QString filename_)
{
	this->filename = filename_;
}


bool GuiClientApplication::event(QEvent* event)
{
	if (event->type() == QEvent::FileOpen)
	{
		// We need to remember the filename, in case the event comes in before our GUI is fully constructed and the signal is connected.
		setOpenFilename(static_cast<QFileOpenEvent*>(event)->file());
		
		emit openFileOSX(getOpenFilename());
	}
	return QApplication::event(event);
}
