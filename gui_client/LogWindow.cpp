/*=====================================================================
LogWindow.cpp
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LogWindow.h"


#include <QtCore/QSettings>
#include <qt/QtUtils.h>
#include <StringUtils.h>


LogWindow::LogWindow(
	QWidget* parent,
	QSettings* settings_
)
:	settings(settings_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("LogWindow/geometry").toByteArray());
	this->restoreState(settings->value("LogWindow/windowState").toByteArray());

	openServerScriptLogLabel->setText(QtUtils::toQString("<a href=\"https://substrata.info/script_log\"><span>Show server script log</span></a>"));
}


LogWindow::~LogWindow()
{
}


void LogWindow::closeEvent(QCloseEvent * event)
{
	saveWindowState();

	QMainWindow::closeEvent(event);
}


void LogWindow::on_openServerScriptLogLabel_linkActivated(const QString& link)
{
	// Emit a signal instead of opening the link directly, so that MainWindow can get the hostname we are connected to and use that in the URL.
	emit openServerScriptLogSignal();
}


void LogWindow::saveWindowState()
{
	// Save main window geometry and state
	settings->setValue("LogWindow/geometry", saveGeometry());
	settings->setValue("LogWindow/windowState", saveState());
}


void LogWindow::appendLine(const std::string& msg)
{
	// maximumBlockCount is basically the max number of lines allowed in the text edit control.  This should have been set to a value > 0 (to enable it) in Qt designer.
	assert(this->logPlainTextEdit->maximumBlockCount() > 0);

	// Because appendPlainText() adds a newline to the end of the appended text, remove any newline from message_str so we don't end up with two newlines.
	this->logPlainTextEdit->appendPlainText(QtUtils::toQString(::eatSuffix(msg, "\n")));
}
